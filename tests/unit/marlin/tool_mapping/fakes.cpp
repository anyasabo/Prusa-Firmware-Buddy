#include "fakes.hpp"

#include <module/motion.h>
#include <module/planner.h>
#include <module/temperature.h>
#include <module/tool_change.h>
#include <module/prusa/tool_mapper.hpp>
#include <mapi/parking.hpp>
#include <mapi/motion.hpp>
#include <pause_stubbed.hpp>
#include <feature/print_status_message/print_status_message_mgr.hpp>
#include <feature/filament_tracker/filament_tracker.hpp>
#include <feature/nozzle_cleaner/include/nozzle_cleaner.hpp>

FakeTools fake_tools;
xyze_pos_t current_position {};
xyz_pos_t hotend_currently_applied_offset {};
FakeThermalManager thermalManager;
FakePlanner planner;

void do_blocking_move_to(const xyz_pos_t &, const feedRate_t &) {}
void sync_e_position_to(float) {}

bool tool_change(const std::variant<VirtualToolIndex, PhysicalToolIndex, NoTool> new_tool, tool_return_t, tool_change_lift_t, bool) {
    fake_tools.tool_changes.push_back(new_tool);
    return fake_tools.tool_change_result;
}

FakePrintStatusMessageMgr &print_status_message() {
    static FakePrintStatusMessageMgr m;
    return m;
}

buddy::FakeFilamentTracker &buddy::filament_tracker() {
    static FakeFilamentTracker t;
    return t;
}

namespace mapi {
ParkingPosition get_parking_position(ParkPosition) { return {}; }
bool park(const ParkingPosition &) { return true; }
void restore_retracted_distance(float, float) {}
} // namespace mapi

Pause &Pause::Instance() {
    static Pause p;
    return p;
}
bool Pause::perform(LoadType, const pause::Settings &) { return true; }

bool nozzle_cleaner::load_and_execute(Sequence) { return true; }

// ---------------------------------------------------------------- tool index (src/common/tool_index.cpp, toolchanger + tool mapping)

template <>
bool PhysicalToolIndex::is_enabled() const {
    const auto &self = static_cast<const PhysicalToolIndex &>(*this);
    return fake_tools.enabled.test(self.to_raw());
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

std::variant<VirtualToolIndex, ToolNotMapped> GcodeToolIndexExtension::to_virtual(const ToolMapper &mapper) const {
    const auto &self = static_cast<const GcodeToolIndex &>(*this);
    return mapper.to_virtual(self);
}

std::variant<VirtualToolIndex, ToolNotMapped> GcodeToolIndexExtension::to_virtual() const {
    return to_virtual(tool_mapper);
}
