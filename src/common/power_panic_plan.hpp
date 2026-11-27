/// @file
/// Decisions of the power panic save and resume, with plain inputs and outputs.
///
/// power_panic.cpp reads the printer state, calls these functions and applies the result. Nothing
/// here touches the hardware, the planner or the storage, so the host tests can call it.
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include <feature/input_shaper/input_shaper_config.hpp>

namespace power_panic::plan {

/// Whether the resume can start without a confirmation from the user.
///
/// It can if nothing was extruded in this print yet, if the bed has no target, or if the bed is
/// colder than its target by less than max_bed_diff.
bool can_auto_recover(float odometer_e_start, float extruded_all, int16_t target_bed, float current_bed_temp, float max_bed_diff);

/// Whether the resume converts the saved native positions to the frame with the skew correction.
///
/// The ISR saved them while a G28, G29 or G12 had the skew off (skew_disabled). A pause starts only
/// between commands, and after a crash the skew guard unwound before the power loss, so in these
/// two cases the count does not tell the frame.
bool needs_skew_conversion(bool skew_disabled, bool was_paused, bool was_crashed);

/// The marlin_server state that atomic_finish() hands the resume to.
enum class FinishKind : uint8_t {
    indx_toolchange, ///< finish the interrupted INDX toolchange
    toolcrash, ///< continue the XL tool crash recovery
    pause, ///< go back to the pause
    recovery, ///< replay the interrupted G-code
};

struct FinishInput {
    bool indx_toolchange_in_progress;
    bool crash_in_toolchange_area;
    bool toolchanger_enabled;
    bool was_paused;
};

FinishKind finish_kind(const FinishInput &in);

/// Input of the Resume step of the resume loop.
struct ResumeInput {
    bool crash_in_toolchange_area; ///< XL with tool crash recovery: crash position in the dock area
    bool indx_toolchange_in_progress;
    bool recover_axis_state; ///< Crash_s::RECOVER_AXIS_STATE of the interrupted G-code
    bool x_or_y_homed; ///< X or Y were homed at the power loss
    bool was_paused;
    float current_z; ///< the saved Z, after the Z alignment of the power panic
    float crash_z; ///< Z of the saved crash position
    float z_homing_height;
};

enum class ResumeNext : uint8_t {
    toolcrash_finish, ///< set the toolchanger return data, then go to Finish
    finish, ///< go to Finish (INDX toolchange recovery homes on its own)
    park_for_pause,
    wait_for_heaters, ///< set the nozzle targets first
};

struct ResumeSteps {
    ResumeNext next;
    /// If set, enqueue rehome_gcode() of this Z lift before the next step.
    std::optional<float> rehome_z_lift;
};

ResumeSteps resume_steps(const ResumeInput &in);

/// Z lift for the XY rehome, so that the nozzle is z_homing_height above the crash position.
float rehome_z_lift(float current_z, float crash_z, float z_homing_height);

using RehomeGcode = std::array<char, 24>;

/// "G28 X Y D R<z_lift>", 0-terminated.
RehomeGcode rehome_gcode(float z_lift);

struct Xyz {
    float x;
    float y;
    float z;

    friend bool operator==(const Xyz &, const Xyz &) = default;
};

/// Position that the Unpark step moves to. The axes that the interrupted G-code does not recover
/// stay where they are.
Xyz unpark_target(const Xyz &saved, const Xyz &current, bool recover_xy, bool recover_z);

/// The tool that the resume data (and thus the G-code replay) uses on INDX.
///
/// During a toolchange, the saved active tool is the tool before the toolchange, so use the target
/// tool.
uint8_t resume_tool(uint8_t saved_active_tool, uint8_t toolchange_tool, bool toolchange_in_progress);

/// The tool that the INDX head holds at the resume: the target tool only after the lock phase.
uint8_t held_tool(uint8_t saved_active_tool, uint8_t toolchange_tool, bool toolchange_after_lock);

/// Value of the saved chamber target if the chamber has no target.
inline constexpr uint16_t chamber_temp_off = 0xffff;

uint16_t save_chamber_target(std::optional<float> target);
std::optional<float> restore_chamber_target(uint16_t saved);

/// The storage has no std::optional, so a frequency of 0 marks a disabled axis.
input_shaper::AxisConfig save_axis_config(const std::optional<input_shaper::AxisConfig> &config);
std::optional<input_shaper::AxisConfig> restore_axis_config(const input_shaper::AxisConfig &saved);

input_shaper::WeightAdjustConfig save_weight_adjust(const std::optional<input_shaper::WeightAdjustConfig> &config);
std::optional<input_shaper::WeightAdjustConfig> restore_weight_adjust(const input_shaper::WeightAdjustConfig &saved);

/// The move of the SaveState step out of the print area, after the state is saved.
struct EscapeInput {
    float x;
    float y;
    float rect_min_x; ///< print area bounding rectangle
    float rect_min_y;
    float rect_max_x;
    float rect_max_y;
    float x_min_print_pos;
    float x_max_print_pos;
    float y_min_print_pos;
    float y_max_print_pos;
    float x_axis_length; ///< X_MAX_POS - X_MIN_POS
    bool corexy; ///< the head can escape along Y too
    bool xy_known; ///< X and Y were homed at the power loss
};

enum class EscapeAxis : uint8_t {
    x,
    y,
};

struct EscapeMove {
    EscapeAxis axis;
    float target;

    friend bool operator==(const EscapeMove &, const EscapeMove &) = default;
};

EscapeMove escape_move(const EscapeInput &in);

} // namespace power_panic::plan
