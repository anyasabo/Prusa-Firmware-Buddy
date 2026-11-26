#include "fake_world.hpp"

#include <module/motion.h>
#include <module/planner.h>
#include <module/stepper.h>
#include <Marlin.h>
#include <timing.h>
#include <config_store/store_instance.hpp>
#include <marlin_server.hpp>
#include <mapi/parking.hpp>
#include <odometer.hpp>
#include <power_panic.hpp>
#include <tasks.hpp>
#include <feature/gcode_exception/gcode_exception.hpp>
#include <feature/indx_tool_lock_hack/indx_tool_lock_hack.hpp>
#include <Marlin/src/feature/bedlevel/bedlevel.h>
#include <Marlin/src/gcode/gcode.h>
#include <tool/hotend/hotend/indx_hotend.hpp>

#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>

FakeWorld world;

// ---------------------------------------------------------------- Marlin globals

xyze_pos_t current_position {};
xyze_pos_t destination {};
feedRate_t feedrate_mm_s = 100;
std::atomic<uint8_t> active_extruder = PrusaToolChanger::MARLIN_NO_TOOL_PICKED;
StrongIndexArray<xyz_pos_t, HOTENDS, PhysicalToolIndex, PhysicalToolIndex::to_raw_static, strong_index_array::AllowWeakIndexing::yes> hotend_offset {};
xyz_pos_t hotend_currently_applied_offset {};
AxesHomeLevel axes_home_level = AxesHomeLevel::no_axes_homed;
toolchange_settings_t toolchange_settings { 2.0f };
FakePlanner planner;
FakeLoadcell loadcell;
FakeStepperE stepperE0;

/// The position that the fake planner thinks the carriage has (machine coordinates).
/// The steppers move the carriage by the difference to the new target. Before homing
/// (after a reboot) this differs from the physical carriage position.
static MachinePosXYZ fw_machine {};

// ---------------------------------------------------------------- tool index (copy of src/common/tool_index.cpp for a toolchanger)

template <>
bool PhysicalToolIndex::is_enabled() const {
    const auto &self = static_cast<const PhysicalToolIndex &>(*this);
    return prusa_toolchanger.is_tool_enabled(self, {});
}

template <>
bool VirtualToolIndex::is_enabled() const {
    const auto &self = static_cast<const VirtualToolIndex &>(*this);
    return self.to_physical().is_enabled();
}

template <>
bool GcodeToolIndex::is_enabled() const {
    const auto &self = static_cast<const GcodeToolIndex &>(*this);
    return match(
        self.to_virtual(),
        [](VirtualToolIndex t) { return t.is_enabled(); },
        [](ToolNotMapped) { return false; });
}

PhysicalToolIndex VirtualToolIndexExtension::to_physical() const {
    const auto &self = static_cast<const VirtualToolIndex &>(*this);
    return PhysicalToolIndex::from_raw(self.to_raw());
}

std::variant<VirtualToolIndex, ToolNotMapped> GcodeToolIndexExtension::to_virtual() const {
    const auto &self = static_cast<const GcodeToolIndex &>(*this);
    return VirtualToolIndex::from_raw(self.to_raw());
}

std::variant<PhysicalToolIndex, NoTool> PhysicalToolIndexExtension::currently_selected() {
    const uint8_t extruder = active_extruder.load();
    if (extruder == PrusaToolChanger::MARLIN_NO_TOOL_PICKED) {
        return NoTool {};
    }
    return PhysicalToolIndex::from_raw(extruder);
}

std::variant<VirtualToolIndex, NoTool> PhysicalToolIndexExtension::currently_selected_virtual_tool() const {
    const auto &self = static_cast<const PhysicalToolIndex &>(*this);
    return VirtualToolIndex::from_raw(self.to_raw());
}

std::variant<PhysicalToolIndex, NoTool> PhysicalToolIndexExtension::from_raw_notool(uint8_t index) {
    if (index == PrusaToolChanger::MARLIN_NO_TOOL_PICKED) {
        return NoTool {};
    }
    return PhysicalToolIndex::from_raw(index);
}

// ---------------------------------------------------------------- hotends

static std::array<IndxHotend, PhysicalToolIndex::count> hotends;

IndxHotend &fake_hotend(int tool) {
    return hotends[tool];
}

IndxHotend::Tool IndxHotend::indx_tool(PhysicalToolIndex tool) {
    return Tool { &hotends[tool.to_raw()] };
}

void IndxHotend::assert_thermally_managed_invariant(std::variant<PhysicalToolIndex, NoTool> expected_managed) {
    for (auto t : PhysicalToolIndex::all()) {
        if (stdext::holds_value(expected_managed, t)) {
            continue;
        }
        if (IndxHotend::indx_tool(t).hotend().is_thermally_managed()) {
            bsod_unreachable();
        }
    }
}

// ---------------------------------------------------------------- world

float FakeWorld::mbl(float x, float y) const {
    // A tilted mesh: 0.1 mm at the origin, rising 0.4 mm over the bed in X and 0.2 mm in Y
    return 0.1f + 0.4f * x / X_BED_SIZE + 0.2f * y / Y_BED_SIZE;
}

float fake_z_value(uint8_t x, uint8_t y) {
    return world.mbl(x * X_BED_SIZE / (GRID_MAX_POINTS_X - 1), y * Y_BED_SIZE / (GRID_MAX_POINTS_Y - 1));
}

xyz_pos_t FakeWorld::nozzle_tip() const {
    xyz_pos_t tip;
    tip.x = carriage.x;
    tip.y = carriage.y;
    tip.z = carriage.z;
    if (head_tool) {
        tip += tool_offsets[*head_tool];
    }
    return tip;
}

std::optional<int> FakeWorld::dock_at_carriage(float y_tolerance) const {
    for (int i = 0; i < tool_count; ++i) {
        if (std::abs(carriage.x - dock_pos[i].x) <= dock_tolerance && std::abs(carriage.y - dock_pos[i].y) <= y_tolerance) {
            return i;
        }
    }
    return std::nullopt;
}

void FakeWorld::before_move() {
    if (power_panic) {
        // The power panic stopped the planner. Nothing moves any more.
        throw FakePowerLoss {};
    }
    if (on_before_move) {
        on_before_move();
    }
    if (power_loss_at_move >= 0 && static_cast<int>(moves.size()) == power_loss_at_move) {
        trigger_power_panic();
    }
}

void FakeWorld::trigger_power_panic() {
    power_panic = true;
    draining = true;
    // ac_fault_isr() saves the phase before the ScopeGuard of tool_change() clears it
    pp_snapshot = PowerPanicSnapshot {};
    pp_snapshot->phase = prusa_toolchanger.phase();
    throw FakePowerLoss {};
}

void FakeWorld::complete_power_panic_save() {
    // panic_loop() (power_panic.cpp, PPState::SaveState) runs after tool_change() returned
    pp_snapshot->return_data = prusa_toolchanger.return_data();
    pp_snapshot->active_tool = PhysicalToolIndex::currently_selected();
    pp_snapshot->z_native = current_position.z;
    pp_snapshot->max_printed_z = planner.max_printed_z;
}

void FakeWorld::record_line(FakeMove::Kind kind, const MachinePosXYZ &target) {
    before_move();
    const MachinePosXYZ from = carriage;
    MachinePosXYZ to = carriage;
    to.x += target.x - fw_machine.x;
    to.y += target.y - fw_machine.y;
    to.z += target.z - fw_machine.z;
    fw_machine = target;

    moves.push_back({ kind, from.x, from.y, from.z, to.x, to.y, to.z, 0 });

    // Lateral motion in front of the docks hits the parked nozzles
    const bool lateral = std::abs(to.x - from.x) > 0.01f;
    const float min_y = std::min(from.y, to.y);
    if (lateral && min_y < PrusaToolChanger::SAFE_Y_WITH_TOOL - 0.01f) {
        char buf[160];
        snprintf(buf, sizeof(buf), "lateral move in the dock area: (%.2f, %.2f) -> (%.2f, %.2f)", (double)from.x, (double)from.y, (double)to.x, (double)to.y);
        violations.emplace_back(buf);
    }
    if (lateral && min_y < Y_DOCK_PARKING_MIN_SAFE_POS) {
        ++lateral_moves_below_parking_safe_y;
    }

    carriage = to;

    // A held nozzle that enters an occupied dock hits the parked nozzle.
    // A closed (locked) empty head that enters an occupied dock hits the nozzle too.
    if (auto dock = dock_at_carriage(5.0f); dock && dock_content[*dock]) {
        if (head_tool) {
            violations.push_back("held tool " + std::to_string(*head_tool) + " moved into occupied dock " + std::to_string(*dock));
        } else if (lock_pos > 0.01f) {
            violations.push_back("closed head moved into occupied dock " + std::to_string(*dock));
        }
    }
}

void FakeWorld::e_service_move(float delta) {
    before_move();
    moves.push_back({ FakeMove::Kind::e_service, carriage.x, carriage.y, carriage.z, carriage.x, carriage.y, carriage.z, delta });
    const float before = lock_pos;
    lock_pos = std::clamp(lock_pos + delta, 0.0f, lock_closed);

    if (lock_pos >= lock_closed - 0.01f && before < lock_closed - 0.01f && !head_tool) {
        if (auto dock = dock_at_carriage(dock_tolerance); dock && dock_content[*dock]) {
            if (lock_grab_failures > 0) {
                --lock_grab_failures;
            } else {
                head_tool = dock_content[*dock];
                dock_content[*dock].reset();
            }
        }
    }

    if (lock_pos <= 0.01f && before > 0.01f && head_tool) {
        if (release_failures > 0) {
            --release_failures;
            return;
        }
        auto dock = dock_at_carriage(dock_tolerance);
        if (dock && !dock_content[*dock]) {
            dock_content[*dock] = head_tool;
        } else {
            violations.push_back("tool " + std::to_string(*head_tool) + " released outside an empty dock");
        }
        head_tool.reset();
    }
}

void FakeWorld::reset() {
    prusa_toolchanger.~PrusaToolChanger();
    new (&prusa_toolchanger) PrusaToolChanger();

    auto &store = config_store();
    store = FakeConfigStore {};
    for (auto t : PhysicalToolIndex::all()) {
        dock_pos[t.to_raw()] = { .x = PrusaToolChanger::DOCK_DEFAULT_X_MM[t], .y = PrusaToolChanger::DOCK_DEFAULT_Y_MM };
        store.set_dock_position(t, { PrusaToolChanger::DOCK_DEFAULT_X_MM[t], PrusaToolChanger::DOCK_DEFAULT_Y_MM });
    }
    store.indx_dock_calibrated_mask.set(std::bitset<8>().set());
    store.indx_last_picked_tool_valid.set(true);
    store.set_indx_last_picked_tool(NoTool {});
    prusa_toolchanger.load_tool_info();

    for (auto t : PhysicalToolIndex::all()) {
        hotend_offset[t] = {};
        hotends[t.to_raw()] = IndxHotend {};
    }
    tool_offsets = {};
    active_extruder = PrusaToolChanger::MARLIN_NO_TOOL_PICKED;
    hotend_currently_applied_offset = {};
    axes_home_level = AxesHomeLevel(AxesHomeLevel::array { AxisHomeLevel::full, AxisHomeLevel::full, AxisHomeLevel::full });
    planner = FakePlanner {};
    toolchange_settings.z_raise = 2.0f;
    feedrate_mm_s = 100;
    stepperE0 = FakeStepperE {};

    carriage = { .x = 150, .y = 150, .z = 10 };
    current_position = { 150, 150, 10, 0 };
    destination = current_position;
    fw_machine = carriage;
    head_tool.reset();
    for (int i = 0; i < tool_count; ++i) {
        dock_content[i] = i;
    }
    lock_pos = 0;
    sensor_valid = false;
    sensor_dead = false;
    sensor_blackouts = 0;
    sensor_blackout_active = false;
    lock_grab_failures = 0;
    release_failures = 0;
    homing_fails = false;
    printing = false;
    aborted = false;
    responses.clear();
    dock_answers.clear();
    shown_phases.clear();
    now_ms = 0;
    draining = false;
    power_panic = false;
    power_loss_at_move = -1;
    pp_snapshot.reset();
    on_before_move = nullptr;
    power_loss_in_idle = nullptr;
    moves.clear();
    violations.clear();
    lateral_moves_below_parking_safe_y = 0;
    g28_calls = 0;
    gcode_exceptions().unwinding = false;
}

void FakeWorld::reboot_after_power_loss() {
    prusa_toolchanger.~PrusaToolChanger();
    new (&prusa_toolchanger) PrusaToolChanger();
    prusa_toolchanger.load_tool_info();
    for (auto &h : hotends) {
        h = IndxHotend {};
    }
    active_extruder = PrusaToolChanger::MARLIN_NO_TOOL_PICKED;
    hotend_currently_applied_offset = {};
    // Z is restored from the power panic data, X and Y are lost
    axes_home_level = AxesHomeLevel(AxesHomeLevel::array { AxisHomeLevel::not_homed, AxisHomeLevel::not_homed, AxisHomeLevel::full });
    planner = FakePlanner {};
    // power_panic.cpp resume_loop() restores Z and max_printed_z
    planner.max_printed_z = pp_snapshot ? pp_snapshot->max_printed_z : 0;
    current_position = { 0, 0, pp_snapshot ? pp_snapshot->z_native : 0, 0 };
    fw_machine = to_machine_pos(current_position.xyz());
    sensor_valid = false;
    draining = false;
    power_panic = false;
    power_loss_at_move = -1;
}

// ---------------------------------------------------------------- motion fakes

MachinePosXYZ to_machine_pos(const xyz_pos_t &pos) {
    MachinePosXYZ result = pos.to_tag<MachinePosTag>();
    if (planner.leveling_active) {
        result.z += world.mbl(pos.x, pos.y);
    }
    return result;
}

xyz_pos_t to_native_pos(const MachinePosXYZ &pos) {
    xyz_pos_t result = pos.to_tag<NativePosTag>();
    if (planner.leveling_active) {
        result.z -= world.mbl(pos.x, pos.y);
    }
    return result;
}

void set_current_position(const xyze_pos_t &native) {
    current_position = native;
}

MachinePosXYZE current_machine_position() {
    return to_machine_pos(current_position);
}

void line_to_current_position(const feedRate_t &) {
    // The real one does not apply MBL (see the deprecation note in motion.h)
    world.record_line(FakeMove::Kind::line, current_position.xyz().to_tag<MachinePosTag>());
}

void line_to_machine_pos(const MachinePosXYZE &target, feedRate_t, const MoveHints &) {
    world.record_line(FakeMove::Kind::line, target.xyz());
    current_position = to_native_pos(target);
}

void prepare_move_to(xyze_pos_t target, feedRate_t, PrepareMoveHints hints) {
    const float e_delta = target.e - current_position.e;
    if (target.xyz() != current_position.xyz()) {
        world.record_line(FakeMove::Kind::line, to_machine_pos(target.xyz()));
    }
    if (e_delta != 0) {
        if (!hints.move.is_service_extruder_move) {
            throw std::runtime_error("unexpected extrusion during toolchange");
        }
        world.e_service_move(e_delta);
    }
    current_position = target;
}

void sync_plan_position_e(std::optional<uint8_t>) {}

void apply_motion_limits(xyz_pos_t &target) {
    target.x = std::clamp<float>(target.x, X_MIN_POS, X_MAX_POS);
    target.y = std::clamp<float>(target.y, Y_MIN_POS, Y_MAX_POS);
    target.z = std::clamp<float>(target.z, Z_MIN_POS, Z_MAX_POS);
}

void update_software_endstops(const AxisEnum) {}

bool axis_unhomed_error(uint8_t axis_bits, AxisHomeLevel required_level) {
    for (auto axis : { X_AXIS, Y_AXIS, Z_AXIS }) {
        if ((axis_bits & _BV(axis)) && !axes_home_level.is_homed(axis, required_level)) {
            return true;
        }
    }
    return false;
}

feedRate_t homing_feedrate(const AxisEnum) {
    return 50;
}

uint8_t do_homing_move(const AxisEnum axis, const float distance, const feedRate_t, bool, bool) {
    if (axis == Y_AXIS) {
        // bump_to_dock: the move stops when the head hits the nozzle in the dock
        auto target = current_position;
        target.y += distance;
        bool hit = false;
        for (int i = 0; i < FakeWorld::tool_count; ++i) {
            const auto &dock = world.dock_pos[i];
            if (std::abs(world.carriage.x - dock.x) <= FakeWorld::dock_tolerance && world.dock_content[i] && target.y < dock.y + 10) {
                target.y = dock.y + 10;
                hit = true;
            }
        }
        world.record_line(FakeMove::Kind::homing, to_machine_pos(target.xyz()));
        current_position = target;
        return hit;
    }
    auto target = current_position;
    target[axis] += distance;
    world.record_line(FakeMove::Kind::homing, to_machine_pos(target.xyz()));
    current_position = target;
    return 0;
}

void do_blocking_move_to_xy(const float &rx, const float &ry, const feedRate_t &) {
    auto target = current_position;
    target.x = rx;
    target.y = ry;
    world.record_line(FakeMove::Kind::line, to_machine_pos(target.xyz()));
    current_position = target;
}

void FakePlanner::synchronize() {}

bool FakePlanner::draining() const {
    return world.draining;
}

void set_bed_leveling_enabled(bool enable) {
    // Same as lib/Marlin/Marlin/src/feature/bedlevel/bedlevel.cpp: the machine position stays
    if (enable == planner.leveling_active) {
        return;
    }
    const auto orig_machine_position = current_machine_position();
    planner.leveling_active = enable;
    set_current_position(to_native_pos(orig_machine_position));
}

bool GcodeSuite::G28_no_parser(bool X, bool Y, bool, const G28Flags &flags) {
    ++world.g28_calls;
    if (world.homing_fails) {
        return false;
    }
    const AxisHomeLevel level = flags.precise ? AxisHomeLevel::full : AxisHomeLevel::imprecise;
    if (flags.only_if_needed && axes_home_level.is_homed(X_AXIS, level) && axes_home_level.is_homed(Y_AXIS, level)) {
        return true;
    }
    // The real G28 turns MBL off (RESTORE_LEVELING_AFTER_G28 is off) and lifts Z to Z_HOMING_HEIGHT
    set_bed_leveling_enabled(false);
    if (current_position.z < 4) {
        auto t = current_position;
        t.z = 4;
        world.record_line(FakeMove::Kind::homing, to_machine_pos(t.xyz()));
        current_position = t;
    }
    // CORE One INDX: Y homes first, to Y max (away from the docks), then X to X min
    if (Y) {
        MachinePosXYZ t = fw_machine;
        t.y += 1000;
        world.record_line(FakeMove::Kind::homing, t);
        world.carriage.y = Y_MAX_POS;
    }
    if (X) {
        MachinePosXYZ t = fw_machine;
        t.x -= 1000;
        world.record_line(FakeMove::Kind::homing, t);
        world.carriage.x = X_MIN_POS;
    }
    axes_home_level[X_AXIS] = level;
    axes_home_level[Y_AXIS] = level;
    current_position.set(to_native_pos(world.carriage).xy());
    fw_machine = to_machine_pos(current_position.xyz());
    return true;
}

namespace mapi {
bool park(const ParkingPosition &pos) {
    xyz_pos_t dest = current_position.xyz();
    if (auto *x = std::get_if<float>(&pos.x)) {
        dest.x = *x;
    }
    if (auto *y = std::get_if<float>(&pos.y)) {
        dest.y = *y;
    }
    if (auto *z = std::get_if<float>(&pos.z)) {
        dest.z = *z;
    } else if (auto *rel = std::get_if<ParkingPosition::Relative>(&pos.z)) {
        dest.z = std::min<float>(dest.z + rel->delta, Z_MAX_POS);
    } else if (auto *al = std::get_if<ParkingPosition::AtLeast>(&pos.z)) {
        float floor = al->absolute;
        if (!std::isnan(al->above_print)) {
            floor = std::max(floor, planner.max_printed_z + al->above_print);
        }
        dest.z = std::min<float>(std::max(dest.z, floor), Z_MAX_POS);
    }

    // The real park() runs pre_park_move_pattern() for the nozzle cleaner at X > X_WASTEBIN_SAFE_POINT
    // (about 300 mm on the CORE One L). The fake leaves it out, so the tests must stay below that X.
    if (dest.x > 299.f) {
        throw std::runtime_error("fake mapi::park() does not model the nozzle cleaner area");
    }
    const auto move_to = [](xyz_pos_t t) {
        world.record_line(FakeMove::Kind::park, to_machine_pos(t));
        current_position.set(t);
    };
    const auto move_xy = [&]() -> bool {
        if (dest.xy() == current_position.xy()) {
            return true;
        }
        if (!axes_home_level.is_homed({ X_AXIS, Y_AXIS }, AxisHomeLevel::imprecise)) {
            return false;
        }
        // Same as src/common/mapi/parking.cpp on INDX: leave the dock area perpendicularly first
        if (current_position.y < Y_DOCK_PARKING_MIN_SAFE_POS) {
            move_to(current_position.xyz().with_y(Y_DOCK_PARKING_MIN_SAFE_POS));
        }
        move_to(current_position.xyz().with_x(dest.x).with_y(dest.y));
        return true;
    };
    const bool xy_first = dest.z < current_position.z;
    if (xy_first && !move_xy()) {
        return false;
    }
    if (dest.z != current_position.z) {
        move_to(current_position.xyz().with_z(dest.z));
    }
    if (!xy_first && !move_xy()) {
        return false;
    }
    return !planner.draining();
}
} // namespace mapi

// ---------------------------------------------------------------- other fakes

void idle(bool) {
    if (!world.power_panic && world.power_loss_in_idle && world.power_loss_in_idle()) {
        world.trigger_power_panic();
    }
    world.now_ms += 10;
    if (!world.sensor_dead && !world.sensor_blackout_active) {
        world.sensor_valid = true;
    }
}

void disable_xy_steppers() {}

uint32_t ticks_ms() {
    return world.now_ms;
}

FakeConfigStore &config_store() {
    static FakeConfigStore store;
    return store;
}

Odometer_s &Odometer_s::instance() {
    static Odometer_s o;
    return o;
}

void Odometer_s::add_toolpick(PhysicalToolIndex) {
    ++toolpicks;
}

namespace power_panic {
bool panic_is_active() {
    return world.power_panic;
}
bool state_stored() {
    return false;
}
} // namespace power_panic

bool TaskDeps::check(TaskDeps::Dependency) {
    return true;
}

FakeGcodeExceptions &gcode_exceptions() {
    static FakeGcodeExceptions e;
    return e;
}

buddy::IndxToolLockHack &buddy::indx_tool_lock_hack() {
    static IndxToolLockHack h;
    return h;
}

buddy::puppies::FakeIndx buddy::puppies::indx;

std::optional<bool> buddy::puppies::FakeIndx::get_nozzle_present() const {
    if (!world.sensor_valid) {
        return std::nullopt;
    }
    return world.head_tool.has_value();
}

void buddy::puppies::FakeIndx::invalidate_nozzle_data() {
    world.sensor_valid = false;
    world.sensor_blackout_active = world.sensor_blackouts > 0;
    if (world.sensor_blackouts > 0) {
        --world.sensor_blackouts;
    }
}

[[noreturn]] void fatal_error(const ErrCode, ...) {
    throw std::runtime_error("fatal_error(ErrCode)");
}

namespace marlin_server {
bool is_printing() {
    return world.printing;
}
void print_abort() {
    world.aborted = true;
}
FSM_Holder::FSM_Holder(PhaseNozzleMismatch phase) {
    world.shown_phases.push_back(phase);
}
FSM_Holder::~FSM_Holder() = default;
void fsm_change(PhaseNozzleMismatch phase) {
    world.shown_phases.push_back(phase);
}
Response wait_for_response(PhaseNozzleMismatch) {
    if (world.responses.empty()) {
        throw std::runtime_error("unexpected toolchange dialog");
    }
    const Response r = world.responses.front();
    world.responses.pop_front();
    return r;
}
FakeResponseVariant wait_for_response_variant(PhaseNozzleMismatch) {
    if (world.dock_answers.empty()) {
        throw std::runtime_error("unexpected dock selection dialog");
    }
    auto a = world.dock_answers.front();
    world.dock_answers.pop_front();
    return { a };
}
} // namespace marlin_server
