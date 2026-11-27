#pragma once

#include <utils/tristate.hpp>

#include <cstdint>

/// Policy of the precise CoreXY refinement during G28: retries, the instability history, the calibration and the prompts.
/// The policy has no side effects of its own: it does all moves, storage and prompts through RefineHooks.
namespace corexy_refine {

enum class Result : uint8_t {
    success,
    hard_fail, ///< Failed hard, cannot recover
    refine_fail, ///< Failed to refine, but could potentially continue unrefined
    calibrate_from_menu, ///< Needs calibration but cannot do it en-situ. The user needs to abort/ignore and do it from the calibrations menu.
};

/// Answer to the "homing calibration needed" prompt
enum class CalibrationAnswer : uint8_t {
    calibrate,
    skip,
    always,
    never,
    timeout,
};

/// Warning that G28 shows when the refinement fails
enum class FailureWarning : uint8_t {
    refinement_failed,
    calibrate_from_menu,
};

/// Answer to a FailureWarning
enum class FailureAnswer : uint8_t {
    retry,
    abort,
    ignore,
};

struct Config {
    /// PRECISE_HOMING_COREXY_RETRIES
    uint8_t retries;

    /// HAS_SWITCHABLE_HOMING_CALIBRATION(): the user can approve or refuse the calibration
    bool switchable_calibration;

    /// The calibration first measures the motor sensitivity (Trinamic with XY_HOMING_MEASURE_SENS_MIN)
    bool calibrates_sensitivity;
};

struct Flags {
    /// The G28 can run the calibration (no D parameter)
    bool can_calibrate;

    /// The G28 must run the calibration (C parameter)
    bool force_calibrate;
};

/// If at least instability_threshold of the last instability_window refinements were unstable, the policy offers a calibration
inline constexpr int instability_threshold = 6;
inline constexpr int instability_window = 10;

class RefineHooks {
public:
    virtual ~RefineHooks() = default;

    /// \returns whether a quick stop is in progress
    virtual bool draining() = 0;

    /// Moves to the start position of the refinement
    virtual void move_to_start() = 0;

    virtual bool is_calibrated() = 0;

    /// Refines the home position. If calibrate is true, the refinement calibrates the home first.
    /// \returns whether the refinement succeeded
    virtual bool refine(bool calibrate) = 0;

    /// \returns whether the last successful refinement found an unstable home
    virtual bool is_unstable() = 0;

    /// Homes X and Y again
    /// \returns false on a hard failure
    virtual bool rehome() = 0;

    /// Measures the motor sensitivity
    /// \returns false on a hard failure
    virtual bool calibrate_sensitivity() = 0;

    virtual void clear_calibration() = 0;

    virtual uint16_t instability_history() = 0;
    virtual void set_instability_history(uint16_t history) = 0;

    /// The stored user decision about the calibration (Tristate::other: ask)
    virtual Tristate auto_recalibrate() = 0;
    virtual void set_auto_recalibrate(Tristate value) = 0;

    virtual CalibrationAnswer ask_calibration() = 0;
    virtual FailureAnswer ask_failure(FailureWarning warning) = 0;

    /// Stops the motion and aborts the print
    virtual void abort_print() = 0;

    /// Shows the "recalibrating home" status until end_recalibration_status()
    virtual void begin_recalibration_status() = 0;
    virtual void end_recalibration_status() = 0;
};

/// Calibrates the home position, if the user and the flags allow it
Result calibrate(RefineHooks &hooks, const Config &config, const Flags &flags);

/// Does one refinement with retries. Calibrates if necessary.
Result refine_once(RefineHooks &hooks, const Config &config, const Flags &flags);

/// Refines until success, a hard failure, or a user decision.
/// \returns whether G28 can mark X and Y as precisely homed
bool refine(RefineHooks &hooks, const Config &config, const Flags &flags);

} // namespace corexy_refine
