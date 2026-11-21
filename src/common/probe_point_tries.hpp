/// @file
/// The retry policy of one load cell probe point (run_z_probe() in probe.cpp), without the motion.
///
/// run_z_probe() does the moves and the waits. For each event it asks this class what to do
/// next. The host unit tests can then walk a point try by try.
///
/// The policy, as of Prusa firmware 8.0.2:
/// - A point gets at most max_tries tries (MULTIPLE_PROBING, 40 on the CORE One L). Try n is at
///   offset_for_probe_try(n): rings of 1, 7, 13, 19 tries at 0, 2, 4, 6 mm.
/// - Before each descent the load cell is tared. If the tare differs from the reference tare by
///   more than max_tare_offset (the trigger threshold, 125 g), the head lifts and takes a new
///   reference. The try continues.
/// - A safety trip that recovers uses up the try. One that does not recover fails the point.
/// - A descent that does not reach the bed fails the point at once.
/// - A good analysis adds its Z. The point is done after required_successes good tries; its Z is
///   the mean of them. A NOK analysis goes on to the next try.
/// - With single_only, the point ends after its first analysis, good or NOK.
/// - When no try is left, the point fails.
/// - The load cell must stream, and the first reference tare must not stop the probe. Else the point
///   fails before its first try.
#pragma once

#include <cmath>
#include "probe_timing.hpp"

namespace buddy::probe {

class ProbePointTries {
public:
    struct Config {
        int max_tries;
        int required_successes = 1;
        bool single_only = false;
        float max_tare_offset; ///< grams
    };

    /// What run_z_probe() does after an event.
    enum class Next {
        retry, ///< start the next try (if start_try() then returns false, the point failed)
        done, ///< the point has its Z: result()
        fail, ///< the point failed: failure()
    };

    explicit ProbePointTries(const Config &config)
        : config_(config) {}

    /// Starts a try. False if no try is left.
    bool start_try() {
        if (tries_ >= config_.max_tries) {
            fail("tries-exhausted");
            return false;
        }
        ++tries_;
        return true;
    }

    /// XY offset of the current try from the point.
    XYOffset try_offset() const { return offset_for_probe_try(tries_ - 1); }

    /// After the tare of a try: true if the head must lift and take a new reference tare.
    bool tare_needs_new_reference(float offset_from_reference) const {
        return std::abs(offset_from_reference) > config_.max_tare_offset;
    }

    /// A safety stop during the try. @p recovered: the recovery brought the load cell back.
    Next on_safety_trip(bool recovered) {
        if (!recovered) {
            return fail("safety-trip-no-recovery");
        }
        ++trips_;
        return Next::retry;
    }

    /// The point failed before its first try (the load cell does not stream, or the reference tare stopped).
    Next on_setup_failure(const char *reason) { return fail(reason); }

    /// The descent reached its lowest point with no trigger.
    Next on_no_contact() { return fail("bed-not-reached"); }

    /// The analysis classified the probe as good, with this Z.
    Next on_good(float z) {
        z_sum_ += z;
        ++successes_;
        if (successes_ >= config_.required_successes) {
            return Next::done;
        }
        return config_.single_only ? fail("single-try-nok") : Next::retry;
    }

    /// The analysis classified the probe as NOK.
    Next on_nok(const char *reason) {
        last_nok_ = reason;
        return config_.single_only ? fail("single-try-nok") : Next::retry;
    }

    float result() const { return successes_ >= config_.required_successes ? z_sum_ / successes_ : NAN; }
    const char *failure() const { return failure_; }
    const char *last_nok() const { return last_nok_; }
    int tries() const { return tries_; }
    int successes() const { return successes_; }
    int trips() const { return trips_; }

private:
    Next fail(const char *reason) {
        failure_ = reason;
        return Next::fail;
    }

    Config config_;
    int tries_ = 0;
    int successes_ = 0;
    int trips_ = 0;
    float z_sum_ = 0;
    const char *failure_ = nullptr;
    const char *last_nok_ = "none";
};

} // namespace buddy::probe
