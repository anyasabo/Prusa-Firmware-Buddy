#include "power_panic_plan.hpp"

#include <algorithm>
#include <cstdio>

namespace power_panic::plan {

bool can_auto_recover(float odometer_e_start, float extruded_all, int16_t target_bed, float current_bed_temp, float max_bed_diff) {
    if (odometer_e_start >= extruded_all) {
        // nothing has been extruded on the bed so far, it's safe to auto-resume irregardless of temp
        return true;
    }
    if (!target_bed || current_bed_temp >= target_bed) {
        return true;
    }
    return (target_bed - current_bed_temp) < max_bed_diff;
}

bool needs_skew_conversion(bool skew_disabled, bool was_paused, bool was_crashed) {
    return skew_disabled && !was_paused && !was_crashed;
}

FinishKind finish_kind(const FinishInput &in) {
    if (in.indx_toolchange_in_progress) {
        return FinishKind::indx_toolchange;
    }
    if (in.crash_in_toolchange_area && in.toolchanger_enabled) {
        return FinishKind::toolcrash;
    }
    return in.was_paused ? FinishKind::pause : FinishKind::recovery;
}

ResumeSteps resume_steps(const ResumeInput &in) {
    // The same condition as in finish_kind(): else the resume skips the reheat and the rehome, and
    // atomic_finish() hands it to the G-code replay.
    if (in.crash_in_toolchange_area && in.toolchanger_enabled) {
        // Do not reheat, do not unpark, do not rehome: the tool crash recovery does it
        return { .next = ResumeNext::toolcrash_finish, .rehome_z_lift = std::nullopt };
    }
    if (in.indx_toolchange_in_progress) {
        // The toolchange recovery includes the XY rehome
        return { .next = ResumeNext::finish, .rehome_z_lift = std::nullopt };
    }

    ResumeSteps steps {
        .next = in.was_paused ? ResumeNext::park_for_pause : ResumeNext::wait_for_heaters,
        .rehome_z_lift = std::nullopt,
    };
    if (in.recover_axis_state && in.x_or_y_homed) {
        steps.rehome_z_lift = rehome_z_lift(in.current_z, in.crash_z, in.z_homing_height);
    }
    return steps;
}

float rehome_z_lift(float current_z, float crash_z, float z_homing_height) {
    const float z_dist = current_z - crash_z;
    return z_dist < z_homing_height ? z_homing_height - z_dist : 0;
}

RehomeGcode rehome_gcode(float z_lift) {
    RehomeGcode buf {};
    snprintf(buf.data(), buf.size(), "G28 X Y D R%f", (double)z_lift);
    return buf;
}

Xyz unpark_target(const Xyz &saved, const Xyz &current, bool recover_xy, bool recover_z) {
    Xyz target = saved;
    if (!recover_xy) {
        target.x = current.x;
        target.y = current.y;
    }
    if (!recover_z) {
        target.z = current.z;
    }
    return target;
}

uint8_t resume_tool(uint8_t saved_active_tool, uint8_t toolchange_tool, bool toolchange_in_progress) {
    return toolchange_in_progress ? toolchange_tool : saved_active_tool;
}

uint8_t held_tool(uint8_t saved_active_tool, uint8_t toolchange_tool, bool toolchange_after_lock) {
    return toolchange_after_lock ? toolchange_tool : saved_active_tool;
}

uint16_t save_chamber_target(std::optional<float> target) {
    return static_cast<uint16_t>(target.value_or(chamber_temp_off));
}

std::optional<float> restore_chamber_target(uint16_t saved) {
    if (saved == chamber_temp_off) {
        return std::nullopt;
    }
    return saved;
}

input_shaper::AxisConfig save_axis_config(const std::optional<input_shaper::AxisConfig> &config) {
    if (!config) {
        input_shaper::AxisConfig disabled {};
        disabled.frequency = 0.f;
        return disabled;
    }
    return *config;
}

std::optional<input_shaper::AxisConfig> restore_axis_config(const input_shaper::AxisConfig &saved) {
    if (saved.frequency == 0.f) {
        return std::nullopt;
    }
    return saved;
}

input_shaper::WeightAdjustConfig save_weight_adjust(const std::optional<input_shaper::WeightAdjustConfig> &config) {
    if (!config) {
        return { .frequency_delta = 0.f, .mass_limit = 0.f };
    }
    return *config;
}

std::optional<input_shaper::WeightAdjustConfig> restore_weight_adjust(const input_shaper::WeightAdjustConfig &saved) {
    // Not frequency_delta: 0 is the default delta of some printers, also with the weight adjust on.
    if (saved.mass_limit == 0.f) {
        return std::nullopt;
    }
    return saved;
}

EscapeMove escape_move(const EscapeInput &in) {
    if (!in.xy_known) {
        // we might be anywhere, plan some move towards the endstop
        return { .axis = EscapeAxis::x, .target = in.x - in.x_axis_length };
    }
    if (in.corexy
        && std::min(in.x - in.rect_min_x, in.rect_max_x - in.x) > std::min(in.y - in.rect_min_y, in.rect_max_y - in.y)) {
        // Move to the print-area Y edge in the direction of the nearest end of the print area. Both
        // Y_MIN_PRINT_POS and Y_MAX_PRINT_POS exclude any toolchanger dock zone (per-printer
        // config), so this never crosses dock hardware.
        return {
            .axis = EscapeAxis::y,
            .target = in.y < (in.rect_min_y + in.rect_max_y) / 2 ? in.y_min_print_pos : in.y_max_print_pos,
        };
    }
    // Move to X edge of printer in direction of nearest X end of print area. X_*_PRINT_POS excludes
    // per-printer hardware outside the print area (nozzle cleaner / waste bin).
    return {
        .axis = EscapeAxis::x,
        .target = in.x < (in.rect_min_x + in.rect_max_x) / 2 ? in.x_min_print_pos : in.x_max_print_pos,
    };
}

} // namespace power_panic::plan
