#include "corexy_refine_policy.hpp"

#include <bit>

namespace corexy_refine {

namespace {

    class RecalibrationStatus {
    public:
        explicit RecalibrationStatus(RefineHooks &hooks)
            : hooks(hooks) {
            hooks.begin_recalibration_status();
        }
        ~RecalibrationStatus() {
            hooks.end_recalibration_status();
        }
        RecalibrationStatus(const RecalibrationStatus &) = delete;
        RecalibrationStatus &operator=(const RecalibrationStatus &) = delete;

    private:
        RefineHooks &hooks;
    };

    using History = uint16_t;
    static_assert(instability_window <= sizeof(History) * 8);
    constexpr History instability_mask = (1 << instability_window) - 1;

} // namespace

Result calibrate(RefineHooks &hooks, const Config &config, const Flags &flags) {
    // We cannot calibrate -> this are bad, abort
    if (!flags.can_calibrate) {
        return Result::calibrate_from_menu;
    }

    // The calibration is not user-configurable, run it whenever needed
    Tristate calibration_approved = Tristate::yes;

    if (config.switchable_calibration) {
        calibration_approved = flags.force_calibrate ? Tristate::yes : hooks.auto_recalibrate();

        // Prompt the user that we would like to do the calibration (if the calibration was not triggered from gcode)
        if (calibration_approved == Tristate::other) {
            switch (hooks.ask_calibration()) {

            case CalibrationAnswer::calibrate:
            case CalibrationAnswer::timeout:
                calibration_approved = Tristate::yes;
                break;

            case CalibrationAnswer::skip:
                calibration_approved = Tristate::no;
                break;

            case CalibrationAnswer::always:
                calibration_approved = Tristate::yes;
                hooks.set_auto_recalibrate(Tristate::yes);
                break;

            case CalibrationAnswer::never:
                calibration_approved = Tristate::no;
                hooks.set_auto_recalibrate(Tristate::no);
                break;
            }
        }
    }

    // Regardless of whether the calibration will run or not, reset homing instability history
    // In both cases, we want a clean slate so that the user is not bothered with "please recalibrate" right away
    hooks.set_instability_history(0);

    if (calibration_approved == Tristate::no) {
        // User decided to not do the calibration at his own risk -> consider the point refined
        return Result::success;
    }

    RecalibrationStatus status(hooks);

    if (config.calibrates_sensitivity) {
        // Try calibrating motor sensitivity before calibration
        if (!hooks.calibrate_sensitivity()) {
            return Result::hard_fail;
        }

        // After calibrating sensitivity, we need to rehome
        if (!hooks.rehome()) {
            return Result::hard_fail;
        }
    }

    for (uint8_t retry = 0; retry < config.retries; retry++) {
        if (hooks.draining()) {
            return Result::hard_fail;
        }

        if (hooks.refine(true)) {
            return Result::success;
        }

        // Always re-home to ensure we are in the right position
        if (!hooks.rehome()) {
            return Result::hard_fail;
        }
    }

    // If recalibration was attempted and failed, clear the old (now known-invalid) calibration data.
    // This mostly just removes the green tickmark in calibration menu - even
    // if we kept the data, it would try to recalibrate next time anyway.
    hooks.clear_calibration();

    return Result::refine_fail;
}

Result refine_once(RefineHooks &hooks, const Config &config, const Flags &flags) {
    hooks.move_to_start();

    if (flags.force_calibrate || !hooks.is_calibrated()) {
        // Calibration is required, do not even attempt to naively refine
        return calibrate(hooks, config, flags);
    }

    for (uint8_t retry = 0; retry < config.retries; retry++) {
        if (hooks.draining()) {
            return Result::hard_fail;
        }

        if (hooks.refine(false)) {
            // Record whether the refinement found a stable home
            const bool is_unstable = hooks.is_unstable();
            const History history = static_cast<History>((hooks.instability_history() << 1) | is_unstable);
            hooks.set_instability_history(history);

            const auto recent_instability_count = std::popcount(static_cast<History>(history & instability_mask));
            if (flags.can_calibrate && recent_instability_count >= instability_threshold) {
                break; // Break out of the loop -> offer calibration
            }

            return Result::success;
        }

        // instead of blindly retrying internally on the same location, move the gantry
        if (!hooks.rehome()) {
            return Result::hard_fail;
        }
    }

    // If we've exhausted retries, force calibration
    return calibrate(hooks, config, flags);
}

bool refine(RefineHooks &hooks, const Config &config, const Flags &flags) {
    while (true) {
        const Result result = refine_once(hooks, config, flags);
        switch (result) {

        case Result::success:
            return true;

        case Result::hard_fail:
            return false;

        case Result::refine_fail:
        case Result::calibrate_from_menu:
            break;
        }

        if (hooks.draining()) {
            // We're quick stopping, do not repeat
            return false;
        }

        const auto warning = (result == Result::calibrate_from_menu) ? FailureWarning::calibrate_from_menu : FailureWarning::refinement_failed;
        switch (hooks.ask_failure(warning)) {

        case FailureAnswer::retry:
            continue;

        case FailureAnswer::abort:
            hooks.abort_print();
            return false;

        case FailureAnswer::ignore:
            // The user decided to ignore the problem at his own risk, consider the homing calibrated for now
            return true;
        }
    }
}

} // namespace corexy_refine
