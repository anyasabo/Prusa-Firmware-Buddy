/// @file
/// Host tests of the INDX toolchanger state machine (toolchanger_indx.cpp, toolchanger_common.cpp,
/// toolchanger_utils_indx.cpp), against the physical model in fake_world.cpp.
#include "fake_world.hpp"

#include <config_store/store_instance.hpp>
#include <module/planner.h>
#include <Marlin/src/feature/bedlevel/bedlevel.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <random>

using Catch::Matchers::WithinAbs;

namespace {

constexpr float eps = 1e-3f;

PhysicalToolIndex T(int i) {
    return PhysicalToolIndex::from_raw(i);
}

std::optional<int> fw_tool() {
    return match(
        PhysicalToolIndex::currently_selected(),
        [](PhysicalToolIndex t) -> std::optional<int> { return t.to_raw(); },
        [](NoTool) -> std::optional<int> { return std::nullopt; });
}

/// Firmware view of the nozzle tip (logical position) in machine coordinates
xyz_pos_t fw_tip() {
    const auto m = to_machine_pos(current_position.xyz());
    return xyz_pos_t { .x = m.x, .y = m.y, .z = m.z } + hotend_currently_applied_offset;
}

bool change(std::variant<PhysicalToolIndex, NoTool> tool, tool_return_t ret = tool_return_t::to_current) {
    return prusa_toolchanger.tool_change(tool, ret, current_position.xyz());
}

/// Give each tool a different offset, the same in the firmware table and in the physical model
void set_offsets(std::mt19937 &rng) {
    std::uniform_real_distribution<float> xy(-2.f, 2.f);
    std::uniform_real_distribution<float> z(-0.6f, 0.6f);
    for (int i = 0; i < FakeWorld::tool_count; ++i) {
        xyz_pos_t o;
        o.x = xy(rng);
        o.y = xy(rng);
        o.z = z(rng);
        hotend_offset[T(i)] = o;
        world.tool_offsets[i] = o;
    }
}

/// The firmware state agrees with the physical state
void check_consistent() {
    INFO("violations: " << (world.violations.empty() ? std::string("none") : world.violations.front()));
    CHECK(world.violations.empty());

    // The active tool is the tool in the head
    CHECK(fw_tool() == world.head_tool);

    // The applied offset is the offset of the active tool, applied once
    const xyz_pos_t expected_offset = world.head_tool ? hotend_offset[T(*world.head_tool)] : xyz_pos_t {};
    CHECK(hotend_currently_applied_offset == expected_offset);

    // Only the active tool is heated
    for (int i = 0; i < FakeWorld::tool_count; ++i) {
        INFO("tool " << i);
        CHECK(fake_hotend(i).is_thermally_managed() == (world.head_tool == i));
    }

    // Each tool is in its own dock or in the head, no tool is lost
    for (int i = 0; i < FakeWorld::tool_count; ++i) {
        INFO("dock " << i);
        if (world.head_tool == i) {
            CHECK(!world.dock_content[i]);
        } else {
            CHECK(world.dock_content[i] == i);
        }
    }

    // The planner position agrees with the carriage (only meaningful while XY is homed)
    if (all_axes_known()) {
        const auto m = to_machine_pos(current_position.xyz());
        CHECK_THAT(m.x, WithinAbs(world.carriage.x, eps));
        CHECK_THAT(m.y, WithinAbs(world.carriage.y, eps));
        CHECK_THAT(m.z, WithinAbs(world.carriage.z, eps));
    }
}

} // namespace

TEST_CASE("toolchanger_indx: pick - swap and park") {
    world.reset();
    std::mt19937 rng(1);
    set_offsets(rng);

    const xyz_pos_t start = fw_tip();

    REQUIRE(change(T(0)));
    check_consistent();
    CHECK(world.head_tool == 0);
    CHECK(config_store().get_indx_last_picked_tool() == std::variant<PhysicalToolIndex, NoTool>(T(0)));

    REQUIRE(change(T(5)));
    check_consistent();
    CHECK(world.head_tool == 5);

    REQUIRE(change(NoTool {}));
    check_consistent();
    CHECK(!world.head_tool);
    CHECK(config_store().get_indx_last_picked_tool() == std::variant<PhysicalToolIndex, NoTool>(NoTool {}));

    // to_current returns the nozzle tip to where it was (Z is lifted by the toolchange, then returns)
    CHECK_THAT(fw_tip().x, WithinAbs(start.x, eps));
    CHECK_THAT(fw_tip().y, WithinAbs(start.y, eps));
    CHECK_THAT(fw_tip().z, WithinAbs(start.z, eps));
}

TEST_CASE("toolchanger_indx: same tool does not move") {
    world.reset();
    REQUIRE(change(T(2)));
    const auto moves = world.moves.size();
    REQUIRE(change(T(2)));
    CHECK(world.moves.size() == moves);
    check_consistent();
}

TEST_CASE("toolchanger_indx: change to a disabled tool is a redscreen") {
    world.reset();
    auto mask = config_store().indx_dock_calibrated_mask.get();
    mask.reset(3);
    config_store().indx_dock_calibrated_mask.set(mask);
    CHECK_THROWS(change(T(3)));
    CHECK(world.moves.empty());
}

TEST_CASE("toolchanger_indx: offsets are applied once over many swaps", "[property]") {
    const auto seed = GENERATE(range(1u, 21u));
    const bool mbl = GENERATE(false, true);
    CAPTURE(seed, mbl);

    world.reset();
    std::mt19937 rng(seed);
    set_offsets(rng);
    planner.leveling_active = mbl;
    current_position.set(to_native_pos(world.carriage));

    std::uniform_int_distribution<int> pick(-1, FakeWorld::tool_count - 1);
    std::uniform_real_distribution<float> px(40.f, 260.f), py(40.f, 240.f), pz(0.3f, 20.f);

    for (int step = 0; step < 40; ++step) {
        // Move somewhere in the print area at the current tool, like a print does between toolchanges
        {
            xyze_pos_t target = current_position;
            target.set(xyz_pos_t { .x = px(rng), .y = py(rng), .z = pz(rng) } - hotend_currently_applied_offset);
            prepare_move_to(target, 100, {});
            planner.max_printed_z = std::max(planner.max_printed_z, current_position.z);
        }
        const xyz_pos_t logical_before = current_position.xyz() + hotend_currently_applied_offset;

        const int t = pick(rng);
        CAPTURE(step, t);
        const std::variant<PhysicalToolIndex, NoTool> target = t < 0 ? std::variant<PhysicalToolIndex, NoTool>(NoTool {}) : T(t);
        REQUIRE(change(target));
        check_consistent();

        // The logical (nozzle) position is the same as before the toolchange
        const xyz_pos_t logical_after = current_position.xyz() + hotend_currently_applied_offset;
        CHECK_THAT(logical_after.x, WithinAbs(logical_before.x, eps));
        CHECK_THAT(logical_after.y, WithinAbs(logical_before.y, eps));
        CHECK_THAT(logical_after.z, WithinAbs(logical_before.z, eps));

        // The physical nozzle tip is at the logical position (MBL is applied at the carriage XY)
        const xyz_pos_t tip = world.nozzle_tip();
        CHECK_THAT(tip.x, WithinAbs(logical_after.x, eps));
        CHECK_THAT(tip.y, WithinAbs(logical_after.y, eps));
        CHECK_THAT(tip.z, WithinAbs(logical_after.z + (mbl ? world.mbl(current_position.x, current_position.y) : 0), eps));

        // MBL is in the state that it was before the toolchange
        CHECK(planner.leveling_active == mbl);
    }
}

TEST_CASE("toolchanger_indx: dock_backoff after park") {
    world.reset();
    REQUIRE(change(T(1)));
    REQUIRE(change(NoTool {}, tool_return_t::dock_backoff));
    check_consistent();
    const auto &info = prusa_toolchanger.get_tool_info(T(1));
    CHECK_THAT(world.carriage.y, WithinAbs(info.dock_y + PrusaToolChanger::DOCK_SAFE_Y_OFFSET + PrusaToolChanger::DOCK_BACKOFF_Y_OFFSET, eps));
}

// OPEN QUESTION (fails, tagged [!shouldfail]): the toolchanger moves in X at dock_y + DOCK_SAFE_Y_OFFSET
// (bump_to_dock(), ensure_tool_homeable(), the end of the mapi::park() moves to the dock). On the CORE One L
// that is -32 + 28.6 = -3.4 mm. Configuration_COREONEL.h says that below Y_DOCK_PARKING_MIN_SAFE_POS
// (-36 + 28.6 + 5 = -2.4 mm) "the motion on X could damage the nozzles or hit ventilation lever".
TEST_CASE("toolchanger_indx: the safe Y of the toolchanger is not below the parking safe Y", "[!shouldfail][open_question]") {
    CHECK(PrusaToolChanger::SAFE_Y_WITH_TOOL >= Y_DOCK_PARKING_MIN_SAFE_POS);

    world.reset();
    REQUIRE(change(T(0)));
    REQUIRE(change(T(7)));
    CHECK(world.lateral_moves_below_parking_safe_y == 0);
}

// ------------------------------------------------------------------ failures

TEST_CASE("toolchanger_indx: pickup fails outside a print") {
    world.reset();
    std::mt19937 rng(7);
    set_offsets(rng);
    REQUIRE(change(T(0)));

    world.lock_grab_failures = 1;
    CHECK_FALSE(change(T(4)));

    // The old tool is parked, nothing is held, XY must be homed again before the next change
    check_consistent();
    CHECK(!world.head_tool);
    CHECK(!all_axes_known());
    CHECK(prusa_toolchanger.get_pickup_fail_count() == 1);
    CHECK(prusa_toolchanger.phase() == PrusaToolChanger::ToolchangePhase::none);

    // The next try homes and succeeds
    const int g28 = world.g28_calls;
    REQUIRE(change(T(4)));
    CHECK(world.g28_calls > g28);
    check_consistent();
}

TEST_CASE("toolchanger_indx: pickup fails once in a print - the retry succeeds") {
    world.reset();
    world.printing = true;
    world.lock_grab_failures = 1;
    REQUIRE(change(T(3)));
    check_consistent();
    CHECK(world.shown_phases.empty());
    CHECK(prusa_toolchanger.get_pickup_fail_count() == 1);
}

TEST_CASE("toolchanger_indx: pickup fails twice in a print - user selects Retry") {
    world.reset();
    world.printing = true;
    world.lock_grab_failures = 2;
    world.responses = { Response::Retry };
    REQUIRE(change(T(3)));
    check_consistent();
    CHECK(world.shown_phases.front() == PhaseNozzleMismatch::pickup_failed);
    CHECK_FALSE(world.aborted);
}

TEST_CASE("toolchanger_indx: pickup fails twice in a print - user aborts") {
    world.reset();
    std::mt19937 rng(3);
    set_offsets(rng);
    world.printing = true;
    REQUIRE(change(T(1)));
    world.lock_grab_failures = 2;
    world.responses = { Response::Abort, Response::Abort };
    CHECK_FALSE(change(T(3)));
    CHECK(world.aborted);
    check_consistent();
    CHECK(!world.head_tool);
}

// KNOWN BUG (fails, tagged [!shouldfail]): park_procedure() stops the heater first. If the nozzle is
// still in the head after the park, park() returns false and the tool stays active, but not thermally
// managed: M104 stores the target but does not apply it (BaseHotend::set_nozzle_target_temp() needs
// is_thermally_managed()), M109 returns at once, and a tool_change() to the same tool is a no-op.
// Only a change to another tool or a reboot restores it.
TEST_CASE("toolchanger_indx: park fails outside a print", "[!shouldfail][known_bug]") {
    world.reset();
    std::mt19937 rng(11);
    set_offsets(rng);
    REQUIRE(change(T(2)));

    world.release_failures = 1;
    CHECK_FALSE(change(T(6)));

    // The tool is still in the head: it must stay the active tool, with its offset and its heater
    check_consistent();
    CHECK(world.head_tool == 2);
    CHECK(prusa_toolchanger.get_park_fail_count() == 1);
}

TEST_CASE("toolchanger_indx: park fails in a print - retries succeed") {
    world.reset();
    world.printing = true;
    REQUIRE(change(T(2)));
    world.release_failures = 2; // park() retries twice before the dialog
    REQUIRE(change(T(6)));
    check_consistent();
    CHECK(world.shown_phases.empty());
}

// KNOWN BUG: the same heater state as in "park fails outside a print". Less important in a print,
// because the abort turns the heaters off.
TEST_CASE("toolchanger_indx: park fails in a print - user aborts", "[!shouldfail][known_bug]") {
    world.reset();
    world.printing = true;
    REQUIRE(change(T(2)));
    world.release_failures = 3;
    world.responses = { Response::Abort, Response::Abort };
    CHECK_FALSE(change(T(6)));
    CHECK(world.aborted);
    check_consistent();
    CHECK(world.head_tool == 2);
}

TEST_CASE("toolchanger_indx: homing fails before the change") {
    world.reset();
    REQUIRE(change(T(2)));
    axes_home_level[X_AXIS] = AxisHomeLevel::not_homed;
    world.homing_fails = true;
    CHECK_FALSE(change(T(5)));
    check_consistent();
    CHECK(world.head_tool == 2);
    CHECK(prusa_toolchanger.phase() == PrusaToolChanger::ToolchangePhase::none);
}

TEST_CASE("toolchanger_indx: nozzle sensor gives no data") {
    world.reset();
    world.sensor_dead = true;
    CHECK_FALSE(change(T(1)));
    // The pickup cannot be verified. The tool is physically locked, but the firmware selected no tool.
    // This is the expected "unknown" state that check_nozzle_presence_vs_eeprom() resolves at the next boot.
    CHECK(world.head_tool == 1);
    CHECK(fw_tool() == std::nullopt);
    CHECK(hotend_currently_applied_offset == xyz_pos_t {});
    CHECK_FALSE(fake_hotend(1).is_thermally_managed());
}

// KNOWN BUG (fails, tagged [!shouldfail]): the retry in pickup() runs open_head() with the nozzle in the
// head. open_head() opens the lock at dock_y + DOCK_UNLOCK_Y_OFFSET, not in the dock, so the model
// releases the nozzle outside the dock. NEEDS A CHECK ON THE PRINTER: if the nozzle stays partly in the
// dock, the next lock move can recover it.
TEST_CASE("toolchanger_indx: nozzle verification times out once in a print - the nozzle is locked", "[!shouldfail][known_bug]") {
    // The lock grabs the nozzle, but the presence data does not come within NOZZLE_VERIFY_TIMEOUT_MS
    // (puppy communication delay). pickup() retries: it homes and runs open_head() with the nozzle in the head.
    world.reset();
    world.printing = true;
    REQUIRE(change(T(1)));
    bool armed = false;
    world.on_before_move = [&armed] {
        if (world.head_tool == 6 && !armed) {
            armed = true;
            world.sensor_blackouts = 1; // the next invalidation is the one before the pickup verification
        }
    };
    world.responses = { Response::Abort, Response::Abort };
    CHECK(change(T(6)));
    check_consistent();
    CHECK(world.head_tool == 6);
}

TEST_CASE("toolchanger_indx: dock not calibrated is a redscreen") {
    world.reset();
    auto pos = config_store().get_dock_position(T(4));
    pos.y += 5; // more than DOCK_INVALID_OFFSET_Y_MM from the default
    config_store().set_dock_position(T(4), pos);
    prusa_toolchanger.load_tool_info();
    CHECK_THROWS(change(T(4)));
    CHECK(world.violations.empty());
}

// pickup_procedure() must check the dock calibration before ensure_head_open(): open_head() reads the dock
// position with check_calibrated=false, so it moved to dock_y + DOCK_UNLOCK_Y_OFFSET of the invalid dock
// before the redscreen.
TEST_CASE("toolchanger_indx: dock not calibrated - no move into the dock area before the redscreen") {
    world.reset();
    auto pos = config_store().get_dock_position(T(4));
    pos.y += 5;
    config_store().set_dock_position(T(4), pos);
    prusa_toolchanger.load_tool_info();
    CHECK_THROWS(change(T(4)));
    for (const auto &m : world.moves) {
        CHECK(std::min(m.y0, m.y1) >= PrusaToolChanger::SAFE_Y_WITH_TOOL - eps);
    }
}

// ------------------------------------------------------------------ power panic

namespace {

/// The resume sequence of src/common/power_panic.cpp (resume_loop) and src/common/marlin_server.cpp
/// (PowerPanic_FinishIndxToolchange, resuming_begin) for a power panic during a toolchange.
bool resume_after_power_panic(bool leveling_before_toolchange = false) {
    world.complete_power_panic_save();
    const PowerPanicSnapshot s = *world.pp_snapshot;
    world.reboot_after_power_loss();

    const uint8_t planner_active_tool = match(
        s.active_tool,
        [](PhysicalToolIndex t) -> uint8_t { return t.to_raw(); },
        [](NoTool) -> uint8_t { return PrusaToolChanger::MARLIN_NO_TOOL_PICKED; });
    const uint8_t tool_nr = match(
        s.return_data.tool,
        [](PhysicalToolIndex t) -> uint8_t { return t.to_raw(); },
        [](NoTool) -> uint8_t { return PrusaToolChanger::MARLIN_NO_TOOL_PICKED; });

    // power_panic.cpp resume_loop(), ResumeState::Resume
    const uint8_t effective_active_tool = s.phase == PrusaToolChanger::ToolchangePhase::after_lock ? tool_nr : planner_active_tool;
    if (effective_active_tool != PrusaToolChanger::MARLIN_NO_TOOL_PICKED) {
        prusa_toolchanger.set_active_extruder(T(effective_active_tool));
        hotend_currently_applied_offset = hotend_offset[T(effective_active_tool)];
    } else {
        prusa_toolchanger.set_active_extruder(NoTool {});
        hotend_currently_applied_offset = xyz_pos_t {};
    }
    const uint8_t resume_active_tool = s.phase != PrusaToolChanger::ToolchangePhase::none ? tool_nr : planner_active_tool;

    // marlin_server.cpp powerpanic_finish_indx_toolchange(): crash_s.leveling_active is the MBL state from before the
    // toolchange (Crash_s::set_toolchange_in_progress()). Then PowerPanic_FinishIndxToolchange (the result is discarded there too).
    if (s.phase != PrusaToolChanger::ToolchangePhase::none) {
        set_bed_leveling_enabled(leveling_before_toolchange);
        (void)prusa_toolchanger.recover_pp_toolchange(s.return_data, PhysicalToolIndex::from_raw_notool(planner_active_tool), s.phase);
    }

    // marlin_server.cpp resuming_begin()
    if (resume_active_tool != PrusaToolChanger::MARLIN_NO_TOOL_PICKED) {
        return prusa_toolchanger.tool_change(T(resume_active_tool), tool_return_t::no_return, current_position.xyz());
    }
    return true;
}

} // namespace

// The power panic tests run without MBL. With MBL, the real Z bookkeeping of a power panic goes through
// crash_s (Crash_s::resume_movement(), restore_state()), and the stubs compile crash detection out.
TEST_CASE("toolchanger_indx: power panic at each move of a swap", "[power_panic]") {
    const auto setup = [] {
        world.reset();
        std::mt19937 rng(5);
        set_offsets(rng);
        world.printing = true;
        REQUIRE(change(T(1)));
    };

    // Count the moves of an undisturbed swap 1 -> 6, with the same offsets
    setup();
    const size_t before = world.moves.size();
    REQUIRE(change(T(6)));
    const int swap_moves = static_cast<int>(world.moves.size() - before);
    REQUIRE(swap_moves > 10);

    const int at = GENERATE_COPY(range(0, swap_moves));
    CAPTURE(at);

    setup();
    const xyz_pos_t logical_before = current_position.xyz() + hotend_currently_applied_offset;
    world.power_loss_at_move = static_cast<int>(world.moves.size()) + at;
    CHECK_THROWS_AS(change(T(6)), FakePowerLoss);
    REQUIRE(world.pp_snapshot);
    const auto phase = world.pp_snapshot->phase;
    CAPTURE(static_cast<int>(phase));

    CHECK(resume_after_power_panic());
    check_consistent();
    CHECK(world.head_tool == 6);

    // finish_pickup_after_lock() returns to the position from before the toolchange (tool_return_t::to_current),
    // under the new tool offset. (In before_lock, tool_change() runs with XY not homed, so it uses no_return,
    // and the resume unpark in power_panic.cpp moves the head back. The fake does not model that unpark.)
    if (phase == PrusaToolChanger::ToolchangePhase::after_lock) {
        const xyz_pos_t logical_after = current_position.xyz() + hotend_currently_applied_offset;
        CHECK_THAT(logical_after.x, WithinAbs(logical_before.x, eps));
        CHECK_THAT(logical_after.y, WithinAbs(logical_before.y, eps));
        CHECK_THAT(logical_after.z, WithinAbs(logical_before.z, eps));
    }
}

TEST_CASE("toolchanger_indx: power panic at each move of a pick from no tool", "[power_panic]") {
    world.reset();
    world.printing = true;
    const size_t before = world.moves.size();
    REQUIRE(change(T(7)));
    const int pick_moves = static_cast<int>(world.moves.size() - before);

    const int at = GENERATE_COPY(range(0, pick_moves));
    CAPTURE(at);

    world.reset();
    world.printing = true;
    world.power_loss_at_move = at;
    CHECK_THROWS_AS(change(T(7)), FakePowerLoss);
    REQUIRE(world.pp_snapshot);
    CAPTURE(static_cast<int>(world.pp_snapshot->phase));

    CHECK(resume_after_power_panic());
    check_consistent();
    CHECK(world.head_tool == 7);
}

TEST_CASE("toolchanger_indx: power panic after a lock that did not grab the nozzle", "[power_panic]") {
    world.reset();
    world.printing = true;
    REQUIRE(change(T(1)));

    // The lock of tool 6 does not grab. The power fails on the first move after the after_lock phase
    // (the exit move from the dock, before the nozzle verification).
    world.lock_grab_failures = 1;
    world.on_before_move = [] {
        if (prusa_toolchanger.phase() == PrusaToolChanger::ToolchangePhase::after_lock && world.power_loss_at_move < 0) {
            world.power_loss_at_move = static_cast<int>(world.moves.size());
        }
    };
    CHECK_THROWS_AS(change(T(6)), FakePowerLoss);
    REQUIRE(world.pp_snapshot);
    REQUIRE(world.pp_snapshot->phase == PrusaToolChanger::ToolchangePhase::after_lock);
    REQUIRE(!world.head_tool);

    world.on_before_move = nullptr;
    CHECK(resume_after_power_panic());
    check_consistent();
    CHECK(world.head_tool == 6);
}

TEST_CASE("toolchanger_indx: power panic after the lock - no presence data at the resume", "[power_panic]") {
    world.reset();
    world.printing = true;
    REQUIRE(change(T(1)));

    // The lock of tool 6 grabs. The power fails on the exit move (after_lock). At the resume, the head gives
    // no presence data for the nozzle check of finish_pickup_after_lock().
    world.on_before_move = [] {
        if (prusa_toolchanger.phase() == PrusaToolChanger::ToolchangePhase::after_lock && world.power_loss_at_move < 0) {
            world.power_loss_at_move = static_cast<int>(world.moves.size());
        }
    };
    CHECK_THROWS_AS(change(T(6)), FakePowerLoss);
    REQUIRE(world.head_tool == 6);
    world.on_before_move = nullptr;
    world.sensor_blackouts = 1;

    // The nozzle is locked: the resume must keep tool 6 and must not open the lock away from the dock
    resume_after_power_panic();
    check_consistent();
    CHECK(world.head_tool == 6);
}

TEST_CASE("toolchanger_indx: power panic during the retry after a failed pickup", "[power_panic]") {
    world.reset();
    world.printing = true;
    REQUIRE(change(T(1)));

    // The first lock of tool 6 does not grab. The retry homes XY and opens the head again.
    // The power fails during that retry, before its lock move.
    world.lock_grab_failures = 1;
    world.on_before_move = [] {
        if (world.g28_calls > 0 && world.power_loss_at_move < 0) {
            world.power_loss_at_move = static_cast<int>(world.moves.size());
        }
    };
    world.g28_calls = 0;
    CHECK_THROWS_AS(change(T(6)), FakePowerLoss);
    REQUIRE(world.pp_snapshot);
    CAPTURE(static_cast<int>(world.pp_snapshot->phase));
    // The retry has not locked anything yet: the phase must not say after_lock
    CHECK(world.pp_snapshot->phase == PrusaToolChanger::ToolchangePhase::before_lock);

    world.on_before_move = nullptr;
    resume_after_power_panic();
    check_consistent();
    CHECK(world.head_tool == 6);
}

TEST_CASE("toolchanger_indx: power panic in the lock dwell - after the nozzle is locked", "[power_panic]") {
    world.reset();
    std::mt19937 rng(9);
    set_offsets(rng);
    world.printing = true;
    REQUIRE(change(T(1)));
    const xyz_pos_t logical_before = current_position.xyz() + hotend_currently_applied_offset;

    // The power fails in the DOCK_DWELL_MS wait after the lock move: the nozzle of tool 6 is
    // locked in the head, but the phase is still before_lock.
    world.power_loss_in_idle = [] {
        return world.head_tool == 6;
    };
    CHECK_THROWS_AS(change(T(6)), FakePowerLoss);
    REQUIRE(world.pp_snapshot);
    CHECK(world.pp_snapshot->phase == PrusaToolChanger::ToolchangePhase::before_lock);
    REQUIRE(world.head_tool == 6);

    world.power_loss_in_idle = nullptr;
    CHECK(resume_after_power_panic());
    check_consistent();
    CHECK(world.head_tool == 6);

    // The pickup returns to the position from before the toolchange, under the offset of tool 6
    const xyz_pos_t logical_after = current_position.xyz() + hotend_currently_applied_offset;
    CHECK_THAT(logical_after.x, WithinAbs(logical_before.x, eps));
    CHECK_THAT(logical_after.y, WithinAbs(logical_before.y, eps));
    CHECK_THAT(logical_after.z, WithinAbs(logical_before.z, eps));
}
