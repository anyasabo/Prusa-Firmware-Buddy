/// Fake of lib/Marlin/Marlin/src/module/motion.h for the INDX toolchanger host tests.
/// It keeps the same names and coordinate conventions: current_position is NATIVE
/// (after the hotend offset, before MBL). The fake world (fake_world.cpp) records each move.
#pragma once

#include <inc/MarlinConfig.h>
#include <core/types.h>
#include <tool_index.hpp>
#include <utils/storage/strong_index_array.hpp>

#include <algorithm>
#include <array>
#include <expected>
#include <atomic>
#include <optional>
#include <span>
#include <utility>

struct MoveHints {
    bool is_printing_move : 1 = false;
    bool extrusion_safety_checks : 1 = true;
    bool ignore_e_factor : 1 = false;
    bool is_service_extruder_move : 1 = false;
};

struct PrepareMoveHints {
    bool scale_feedrate : 1 = true;
    bool do_segment : 1 = true;
    bool apply_motion_limits : 1 = true;
    MoveHints move = {};
};

enum class AxisHomeLevel : uint8_t {
    not_homed,
    imprecise,
    full
};

struct AxesHomeLevel : public std::array<AxisHomeLevel, 3> {
    using array::array;
    using array::operator=;
    AxesHomeLevel(const array &data)
        : array(data) {}
    static constexpr array no_axes_homed { AxisHomeLevel::not_homed, AxisHomeLevel::not_homed, AxisHomeLevel::not_homed };
    constexpr bool is_homed(AxisEnum axis, AxisHomeLevel required_level) const {
        return at(std::to_underlying(axis)) >= required_level;
    }
    constexpr bool is_homed(std::initializer_list<AxisEnum> axes, AxisHomeLevel required_level) const {
        for (auto axis : axes) {
            if (!is_homed(axis, required_level)) {
                return false;
            }
        }
        return true;
    }
    constexpr bool is_homed(AxisHomeLevel required_level) const {
        return is_homed({ X_AXIS, Y_AXIS, Z_AXIS }, required_level);
    }
};

extern AxesHomeLevel axes_home_level;
inline bool all_axes_known(AxisHomeLevel required_level = AxisHomeLevel::imprecise) { return axes_home_level.is_homed(required_level); }

extern xyze_pos_t current_position;
extern xyze_pos_t destination;
extern feedRate_t feedrate_mm_s;
extern std::atomic<uint8_t> active_extruder;
extern StrongIndexArray<xyz_pos_t, HOTENDS, PhysicalToolIndex, PhysicalToolIndex::to_raw_static, strong_index_array::AllowWeakIndexing::yes> hotend_offset;
extern xyz_pos_t hotend_currently_applied_offset;

#define XY_PROBE_FEEDRATE_MM_S MMM_TO_MMS(XY_PROBE_SPEED_INITIAL)

inline xyz_pos_t native_logical_offset() {
    xyz_pos_t result { 0, 0, 0 };
    result += hotend_currently_applied_offset;
    return result;
}

template <size_t s>
[[nodiscard]] auto toLogical(const Vector<float, NativePosTag, s> &v) {
    return (v + native_logical_offset()).template to_tag<LogicalPosTag>();
}

template <size_t s>
[[nodiscard]] auto toNative(const Vector<float, LogicalPosTag, s> &v) {
    return v.template to_tag<NativePosTag>() - native_logical_offset();
}

MachinePosXYZ to_machine_pos(const xyz_pos_t &pos);
xyz_pos_t to_native_pos(const MachinePosXYZ &pos);
inline MachinePosXYZE to_machine_pos(const xyze_pos_t &pos) {
    MachinePosXYZE result = pos.to_tag<MachinePosTag>();
    result.set(to_machine_pos(pos.xyz()));
    return result;
}
inline xyze_pos_t to_native_pos(const MachinePosXYZE &pos) {
    xyze_pos_t result = pos.to_tag<NativePosTag>();
    result.set(to_native_pos(pos.xyz()));
    return result;
}

void set_current_position(const xyze_pos_t &native);
MachinePosXYZE current_machine_position();
void line_to_current_position(const feedRate_t &fr_mm_s = feedrate_mm_s);
void line_to_machine_pos(const MachinePosXYZE &target, feedRate_t fr_mm_s, const MoveHints &hints = {});
void prepare_move_to(xyze_pos_t target, feedRate_t fr_mm_s, PrepareMoveHints hints);
void sync_plan_position_e(std::optional<uint8_t> e_axis_index = std::nullopt);
void apply_motion_limits(xyz_pos_t &target);
void update_software_endstops(const AxisEnum axis);
bool axis_unhomed_error(uint8_t axis_bits = 0x07, AxisHomeLevel required_level = AxisHomeLevel::imprecise);
uint8_t do_homing_move(const AxisEnum axis, const float distance, const feedRate_t fr_mm_s = 0.0, bool can_move_back_before_homing = false, bool homing_z_with_probe = true);
feedRate_t homing_feedrate(const AxisEnum a);
void do_blocking_move_to_xy(const float &rx, const float &ry, const feedRate_t &fr_mm_s = 0.0f);
