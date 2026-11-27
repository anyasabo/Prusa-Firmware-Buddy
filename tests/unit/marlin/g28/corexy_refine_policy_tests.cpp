/// @file
/// Host tests of the precise CoreXY refinement policy of G28
/// (lib/Marlin/Marlin/src/gcode/calibrate/corexy_refine_policy.cpp)
#include <gcode/calibrate/corexy_refine_policy.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <deque>
#include <string>
#include <vector>

using namespace corexy_refine;

namespace {

/// Scripted machine. Each queue gives the next results; an empty queue gives the default.
class FakeHooks final : public RefineHooks {
public:
    bool calibrated = true;
    std::deque<bool> refine_results;
    bool refine_default = true;
    std::deque<bool> unstable_results;
    std::deque<bool> rehome_results;
    bool sens_result = true;
    bool is_draining = false;
    /// If not negative, draining() returns true after this count of calls
    int draining_after_calls = -1;
    int draining_calls = 0;
    uint16_t history = 0;
    Tristate stored_auto = Tristate::other;
    std::deque<CalibrationAnswer> calibration_answers;
    std::deque<FailureAnswer> failure_answers;

    std::vector<std::string> log;
    int status_depth = 0;

    bool draining() override {
        if (draining_after_calls >= 0 && draining_calls++ >= draining_after_calls) {
            return true;
        }
        return is_draining;
    }
    void move_to_start() override {
        log.push_back("move_to_start");
    }
    bool is_calibrated() override {
        return calibrated;
    }
    bool refine(bool calibrate) override {
        log.push_back(calibrate ? "refine_calibrate" : "refine");
        // The status message shows during the whole calibration, and only then
        CHECK(status_depth == (calibrate ? 1 : 0));
        return pop(refine_results, refine_default);
    }
    bool is_unstable() override {
        return pop(unstable_results, false);
    }
    bool rehome() override {
        log.push_back("rehome");
        return pop(rehome_results, true);
    }
    bool calibrate_sensitivity() override {
        log.push_back("sens");
        return sens_result;
    }
    void clear_calibration() override {
        log.push_back("clear");
        calibrated = false;
    }
    uint16_t push_instability(bool unstable) override {
        history = push_instability_bit(history, unstable);
        return history;
    }
    void reset_instability_history() override {
        history = 0;
    }
    Tristate auto_recalibrate() override {
        return stored_auto;
    }
    void set_auto_recalibrate(Tristate value) override {
        stored_auto = value;
    }
    CalibrationAnswer ask_calibration() override {
        log.push_back("ask_calibration");
        REQUIRE_FALSE(calibration_answers.empty());
        return pop(calibration_answers, CalibrationAnswer::timeout);
    }
    FailureAnswer ask_failure(FailureWarning warning) override {
        log.push_back(warning == FailureWarning::calibrate_from_menu ? "ask_calibrate_from_menu" : "ask_refinement_failed");
        REQUIRE_FALSE(failure_answers.empty());
        return pop(failure_answers, FailureAnswer::abort);
    }
    void abort_print() override {
        log.push_back("abort");
    }
    void begin_recalibration_status() override {
        status_depth++;
    }
    void end_recalibration_status() override {
        status_depth--;
    }

    int count(const std::string &entry) const {
        return static_cast<int>(std::count(log.begin(), log.end(), entry));
    }

private:
    template <typename T>
    static T pop(std::deque<T> &queue, T fallback) {
        if (queue.empty()) {
            return fallback;
        }
        const T value = queue.front();
        queue.pop_front();
        return value;
    }
};

constexpr Config coreone { .retries = 6, .switchable_calibration = true, .calibrates_sensitivity = true };
constexpr Config ix { .retries = 3, .switchable_calibration = false, .calibrates_sensitivity = false };

constexpr Flags g28 { .can_calibrate = true, .force_calibrate = false };
constexpr Flags g28_d { .can_calibrate = false, .force_calibrate = false };
constexpr Flags g28_c { .can_calibrate = true, .force_calibrate = true };

using Log = std::vector<std::string>;

} // namespace

TEST_CASE("corexy_refine: a stable refinement succeeds and records a stable bit") {
    FakeHooks hooks;
    hooks.history = 0b1;

    CHECK(refine_once(hooks, coreone, g28) == Result::success);
    CHECK(hooks.log == Log { "move_to_start", "refine" });
    CHECK(hooks.history == 0b10);
    CHECK(hooks.status_depth == 0);
}

TEST_CASE("corexy_refine: an unstable refinement records an unstable bit") {
    FakeHooks hooks;
    hooks.history = 0b1;
    hooks.unstable_results = { true };

    CHECK(refine_once(hooks, coreone, g28) == Result::success);
    CHECK(hooks.history == 0b11);
}

TEST_CASE("corexy_refine: 6 unstable of the last 10 offer the calibration") {
    FakeHooks hooks;
    hooks.history = 0b11111; // 5 unstable
    hooks.unstable_results = { true }; // the 6th
    hooks.stored_auto = Tristate::yes;

    CHECK(refine_once(hooks, coreone, g28) == Result::success);
    CHECK(hooks.log == Log { "move_to_start", "refine", "sens", "rehome", "refine_calibrate" });
    CHECK(hooks.history == 0);
    CHECK(hooks.status_depth == 0);
}

TEST_CASE("corexy_refine: 5 unstable of the last 10 do not offer the calibration") {
    FakeHooks hooks;
    hooks.history = 0b1111; // 4 unstable
    hooks.unstable_results = { true }; // the 5th

    CHECK(refine_once(hooks, coreone, g28) == Result::success);
    CHECK(hooks.log == Log { "move_to_start", "refine" });
    CHECK(hooks.history == 0b11111);
}

TEST_CASE("corexy_refine: unstable bits older than the window do not count") {
    FakeHooks hooks;
    hooks.history = 0b111111 << 9; // 6 unstable, one of them still inside the window after the shift
    hooks.unstable_results = { false };

    CHECK(refine_once(hooks, coreone, g28) == Result::success);
    CHECK(hooks.log == Log { "move_to_start", "refine" });
    CHECK(hooks.history == static_cast<uint16_t>(0b111111 << 10));
}

TEST_CASE("corexy_refine: G28 D does not offer the calibration for an unstable home") {
    FakeHooks hooks;
    hooks.history = 0b11111;
    hooks.unstable_results = { true };

    CHECK(refine_once(hooks, coreone, g28_d) == Result::success);
    CHECK(hooks.log == Log { "move_to_start", "refine" });
    CHECK(hooks.history == 0b111111);
}

TEST_CASE("corexy_refine: a failed refinement rehomes and retries") {
    FakeHooks hooks;
    hooks.refine_results = { false, false, true };

    CHECK(refine_once(hooks, coreone, g28) == Result::success);
    CHECK(hooks.log == Log { "move_to_start", "refine", "rehome", "refine", "rehome", "refine" });
}

TEST_CASE("corexy_refine: a failed rehome is a hard failure") {
    FakeHooks hooks;
    hooks.refine_results = { false };
    hooks.rehome_results = { false };

    CHECK(refine_once(hooks, coreone, g28) == Result::hard_fail);
    CHECK(hooks.log == Log { "move_to_start", "refine", "rehome" });
}

TEST_CASE("corexy_refine: a quick stop is a hard failure") {
    FakeHooks hooks;
    hooks.is_draining = true;

    CHECK(refine_once(hooks, coreone, g28) == Result::hard_fail);
    CHECK(hooks.log == Log { "move_to_start" });
}

TEST_CASE("corexy_refine: all retries fail, then the policy calibrates") {
    FakeHooks hooks;
    hooks.refine_default = false;
    hooks.refine_results = { false, false, false, true };
    hooks.history = 0b101;

    CHECK(refine_once(hooks, ix, g28) == Result::success);
    CHECK(hooks.log == Log { "move_to_start", "refine", "rehome", "refine", "rehome", "refine", "rehome", "refine_calibrate" });
    CHECK(hooks.history == 0);
}

TEST_CASE("corexy_refine: all retries fail with G28 D, then the user must calibrate from the menu") {
    FakeHooks hooks;
    hooks.refine_default = false;

    CHECK(refine_once(hooks, ix, g28_d) == Result::calibrate_from_menu);
    CHECK(hooks.count("refine") == 3);
    CHECK(hooks.count("refine_calibrate") == 0);
}

TEST_CASE("corexy_refine: an uncalibrated home calibrates without a plain refinement") {
    FakeHooks hooks;
    hooks.calibrated = false;

    CHECK(refine_once(hooks, ix, g28) == Result::success);
    CHECK(hooks.log == Log { "move_to_start", "refine_calibrate" });
}

TEST_CASE("corexy_refine: G28 C calibrates a calibrated home") {
    FakeHooks hooks;
    hooks.stored_auto = Tristate::no;

    CHECK(refine_once(hooks, coreone, g28_c) == Result::success);
    CHECK(hooks.log == Log { "move_to_start", "sens", "rehome", "refine_calibrate" });
}

TEST_CASE("corexy_refine: calibrate without permission asks for the menu and keeps the history") {
    FakeHooks hooks;
    hooks.history = 0b111;

    CHECK(calibrate(hooks, coreone, g28_d) == Result::calibrate_from_menu);
    CHECK(hooks.log.empty());
    CHECK(hooks.history == 0b111);
}

TEST_CASE("corexy_refine: the calibration prompt") {
    FakeHooks hooks;
    hooks.history = 0b111;

    SECTION("calibrate") {
        hooks.calibration_answers = { CalibrationAnswer::calibrate };
        CHECK(calibrate(hooks, coreone, g28) == Result::success);
        CHECK(hooks.count("refine_calibrate") == 1);
        CHECK(hooks.stored_auto == Tristate::other);
    }
    SECTION("timeout calibrates") {
        hooks.calibration_answers = { CalibrationAnswer::timeout };
        CHECK(calibrate(hooks, coreone, g28) == Result::success);
        CHECK(hooks.count("refine_calibrate") == 1);
        CHECK(hooks.stored_auto == Tristate::other);
    }
    SECTION("skip") {
        hooks.calibration_answers = { CalibrationAnswer::skip };
        CHECK(calibrate(hooks, coreone, g28) == Result::success);
        CHECK(hooks.log == Log { "ask_calibration" });
        CHECK(hooks.stored_auto == Tristate::other);
    }
    SECTION("always") {
        hooks.calibration_answers = { CalibrationAnswer::always };
        CHECK(calibrate(hooks, coreone, g28) == Result::success);
        CHECK(hooks.count("refine_calibrate") == 1);
        CHECK(hooks.stored_auto == Tristate::yes);
    }
    SECTION("never") {
        hooks.calibration_answers = { CalibrationAnswer::never };
        CHECK(calibrate(hooks, coreone, g28) == Result::success);
        CHECK(hooks.log == Log { "ask_calibration" });
        CHECK(hooks.stored_auto == Tristate::no);
    }

    // Each answer resets the history
    CHECK(hooks.history == 0);
    CHECK(hooks.status_depth == 0);
}

TEST_CASE("corexy_refine: the stored decision replaces the prompt") {
    FakeHooks hooks;

    SECTION("yes") {
        hooks.stored_auto = Tristate::yes;
        CHECK(calibrate(hooks, coreone, g28) == Result::success);
        CHECK(hooks.log == Log { "sens", "rehome", "refine_calibrate" });
    }
    SECTION("no") {
        hooks.stored_auto = Tristate::no;
        CHECK(calibrate(hooks, coreone, g28) == Result::success);
        CHECK(hooks.log.empty());
    }
}

TEST_CASE("corexy_refine: without the switch, the calibration runs without a prompt") {
    FakeHooks hooks;
    hooks.stored_auto = Tristate::no; // not used without the switch

    CHECK(calibrate(hooks, ix, g28) == Result::success);
    CHECK(hooks.log == Log { "refine_calibrate" });
}

TEST_CASE("corexy_refine: sensitivity calibration failures are hard failures") {
    FakeHooks hooks;
    hooks.stored_auto = Tristate::yes;

    SECTION("sensitivity") {
        hooks.sens_result = false;
        CHECK(calibrate(hooks, coreone, g28) == Result::hard_fail);
        CHECK(hooks.log == Log { "sens" });
    }
    SECTION("rehome after the sensitivity") {
        hooks.rehome_results = { false };
        CHECK(calibrate(hooks, coreone, g28) == Result::hard_fail);
        CHECK(hooks.log == Log { "sens", "rehome" });
    }
    CHECK(hooks.status_depth == 0);
}

TEST_CASE("corexy_refine: a failed calibration clears the calibration data") {
    FakeHooks hooks;
    hooks.refine_default = false;

    CHECK(calibrate(hooks, ix, g28) == Result::refine_fail);
    CHECK(hooks.log == Log { "refine_calibrate", "rehome", "refine_calibrate", "rehome", "refine_calibrate", "rehome", "clear" });
    CHECK_FALSE(hooks.calibrated);
    CHECK(hooks.status_depth == 0);
}

TEST_CASE("corexy_refine: a quick stop during the calibration is a hard failure") {
    FakeHooks hooks;
    hooks.is_draining = true;

    CHECK(calibrate(hooks, ix, g28) == Result::hard_fail);
    CHECK(hooks.log.empty());
}

TEST_CASE("corexy_refine: refine loop") {
    FakeHooks hooks;

    SECTION("success") {
        CHECK(refine(hooks, ix, g28));
        CHECK(hooks.log == Log { "move_to_start", "refine" });
    }
    SECTION("hard failure") {
        hooks.refine_results = { false };
        hooks.rehome_results = { false };
        CHECK_FALSE(refine(hooks, ix, g28));
        CHECK(hooks.count("ask_refinement_failed") == 0);
    }
    SECTION("retry after a failed calibration") {
        hooks.refine_default = false;
        hooks.refine_results = { false, false, false, false, false, false, true };
        hooks.failure_answers = { FailureAnswer::retry };
        CHECK(refine(hooks, ix, g28));
        CHECK(hooks.count("ask_refinement_failed") == 1);
        CHECK(hooks.count("move_to_start") == 2);
        // The failed calibration cleared the data, so the second attempt calibrates at once
        CHECK(hooks.log.back() == "refine_calibrate");
    }
    SECTION("ignore") {
        hooks.refine_default = false;
        hooks.failure_answers = { FailureAnswer::ignore };
        CHECK(refine(hooks, ix, g28));
        CHECK(hooks.count("abort") == 0);
    }
    SECTION("abort") {
        hooks.refine_default = false;
        hooks.failure_answers = { FailureAnswer::abort };
        CHECK_FALSE(refine(hooks, ix, g28));
        CHECK(hooks.log.back() == "abort");
    }
    SECTION("calibrate from the menu") {
        hooks.refine_default = false;
        hooks.failure_answers = { FailureAnswer::ignore };
        CHECK(refine(hooks, ix, g28_d));
        CHECK(hooks.count("ask_calibrate_from_menu") == 1);
    }
}

TEST_CASE("corexy_refine: a quick stop after a failed refinement does not prompt") {
    FakeHooks hooks;
    hooks.calibrated = false;
    hooks.refine_default = false;
    // The calibration checks 3 times, then the refine loop sees the quick stop
    hooks.draining_after_calls = 3;

    CHECK_FALSE(refine(hooks, ix, g28));
    CHECK(hooks.count("refine_calibrate") == 3);
    CHECK(hooks.count("ask_refinement_failed") == 0);
}
