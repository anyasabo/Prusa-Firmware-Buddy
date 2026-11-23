#include <feature/pressure_advance_calibration/step_response_analyzer.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace pressure_advance_calibration;
using Catch::Matchers::WithinRel;

namespace {

constexpr float sample_period_s = 1.f / 320.f;
constexpr float filament_area_mm2 = 2.405f;

/// Nozzle model: the filament is compressed by L to push the melt out at v_out = (L / a)^(1 / n).
/// n = 1 is the linear model pressure advance is built on; n < 1 is a shear-thinning melt.
/// The load cell reads k * L.
struct Nozzle {
    float n;
    float a;
    float k;

    /// Nozzle for which the ideal pressure advance between the two velocities is secant_s
    static Nozzle with_secant(float n, float secant_s, float v_lo, float v_hi, float load_hi_g = 1500.f) {
        const float a = secant_s * (v_hi - v_lo) / (std::pow(v_hi, n) - std::pow(v_lo, n));
        return { n, a, load_hi_g / (a * std::pow(v_hi, n)) };
    }

    double outflow(double compression) const { return std::pow(std::max(compression, 0.0) / a, 1.0 / n); }
};

struct SimOptions {
    float accel = 1250.f; ///< extruder acceleration [mm/s^2]
    float noise_g = 1.f; ///< load cell noise standard deviation
    float drift_g_per_s = 0.f; ///< load cell drift
    float timestamp_shift_s = 0.f; ///< error of the sample timestamps
    uint32_t start_time_us = 4'000'000'000u; ///< close to the wrap around of the timer
    size_t drop_every = 0; ///< drop every Nth sample (0 = none)
    float final_position_error_mm = -0.00005f; ///< the motor stops this far from the plan end
    float idle_before_s = 0.2f; ///< extruder standing still before the run
    uint32_t seed = 1;
};

/// Runs the segments on the model and feeds the analyzer like the load cell would.
void simulate(StepResponseAnalyzer &analyzer, const Nozzle &nozzle, float e_start, const std::vector<StepResponseAnalyzer::Segment> &segments, const SimOptions &options = {}) {
    REQUIRE(analyzer.begin(e_start, segments));

    // Integrated in double so that the simulated motion matches the plan exactly
    constexpr double dt = 25e-6;
    std::mt19937 rng(options.seed);
    std::normal_distribution<float> noise(0.f, options.noise_g);

    std::vector<double> ends;
    double total = 0;
    for (const auto &s : segments) {
        total += s.length_mm;
        ends.push_back(total);
    }
    const double run_end = total + options.final_position_error_mm;

    double compression = 0; // the melt is relaxed before the run
    double position = 0;
    double velocity = 0;
    double t = 0;

    // Load cell averages over one conversion period, the firmware timestamps the sample one period
    // before it is read out
    double adc_sum = 0;
    int adc_count = 0;
    double next_sample = sample_period_s;
    size_t sample_index = 0;
    const double settle_after_s = 0.3;
    double stopped_at = -1;

    while (stopped_at < 0 || t < stopped_at + settle_after_s) {
        double target = 0;
        if (t >= options.idle_before_s && position < run_end) {
            const size_t segment = std::lower_bound(ends.begin(), ends.end(), position + 1e-9) - ends.begin();
            target = segment < segments.size() ? segments[segment].length_mm / segments[segment].duration_s : 0;
        }
        const double max_dv = options.accel * dt;
        velocity += std::clamp(target - velocity, -max_dv, max_dv);
        if (position >= run_end) {
            velocity = 0;
            position = run_end;
            if (stopped_at < 0) {
                stopped_at = t;
            }
        }
        position = std::min(position + velocity * dt, run_end);
        compression += (velocity - nozzle.outflow(compression)) * dt;
        t += dt;

        adc_sum += nozzle.k * compression + options.drift_g_per_s * t;
        ++adc_count;
        if (t >= next_sample) {
            const float load = static_cast<float>(adc_sum / adc_count) + noise(rng);
            adc_sum = 0;
            adc_count = 0;
            next_sample += sample_period_s;
            ++sample_index;

            const double stamp = t - sample_period_s + options.timestamp_shift_s;
            if (options.drop_every && sample_index % options.drop_every == 0) {
                continue;
            }
            // Position as the position lookback would report it for the timestamp
            const float e = static_cast<float>(e_start + position - velocity * (t - stamp));
            const uint32_t time_us = options.start_time_us + static_cast<uint32_t>(stamp * 1e6);
            analyzer.add_sample(time_us, load, e);
        }
    }
    analyzer.finish();
}

std::vector<StepResponseAnalyzer::Segment> step_profile(float v_lo, float v_hi, float t_lo, float t_hi, int cycles) {
    std::vector<StepResponseAnalyzer::Segment> segments;
    segments.push_back({ v_hi * 0.4f, 0.4f }); // prime
    segments.push_back({ v_lo * t_lo, t_lo }); // lead-in
    for (int i = 0; i < cycles; ++i) {
        segments.push_back({ v_hi * t_hi, t_hi });
        segments.push_back({ v_lo * t_lo, t_lo });
    }
    return segments;
}

struct Summary {
    float tau_rising;
    float tau_falling;
    float pressure_advance;
};

/// Median time constants of the rising and falling steps, skipping the warm-up segments
Summary summarize(const StepResponseAnalyzer &analyzer, size_t skip = 4) {
    std::vector<float> rising, falling;
    const auto results = analyzer.results();
    for (size_t i = skip; i < results.size(); ++i) {
        REQUIRE(std::isfinite(results[i].tau_s));
        (results[i].rising ? rising : falling).push_back(results[i].tau_s);
    }
    REQUIRE(!rising.empty());
    REQUIRE(!falling.empty());
    const auto med = [](std::vector<float> v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    const float r = med(rising);
    const float f = med(falling);
    return { r, f, combine_time_constants(r, f) };
}

constexpr float v_lo = 1.f / filament_area_mm2;
constexpr float v_hi = 9.f / filament_area_mm2;

} // namespace

TEST_CASE("pressure_advance_calibration::tau_from_lag_coefficient") {
    CHECK_THAT(tau_from_lag_coefficient(std::exp(-1.f), 0.04f), WithinRel(0.04f, 1e-5f));
    CHECK(std::isnan(tau_from_lag_coefficient(0.95f, 0.01f)));
    CHECK(std::isnan(tau_from_lag_coefficient(0.05f, 0.01f)));
    CHECK(std::isnan(tau_from_lag_coefficient(NAN, 0.01f)));
}

TEST_CASE("pressure_advance_calibration::combine_time_constants") {
    CHECK_THAT(combine_time_constants(0.04f, 0.04f), WithinRel(0.04f, 1e-6f));
    CHECK_THAT(combine_time_constants(0.02f, 0.06f), WithinRel(0.03f, 1e-6f));
    CHECK(std::isnan(combine_time_constants(NAN, 0.04f)));
    CHECK(std::isnan(combine_time_constants(0.04f, 0.f)));
}

TEST_CASE("pressure_advance_calibration::StepResponseAnalyzer rejects invalid plans") {
    StepResponseAnalyzer analyzer;
    CHECK_FALSE(analyzer.begin(0, {}));
    CHECK(analyzer.done());

    const std::vector<StepResponseAnalyzer::Segment> zero_length { { 0.f, 1.f } };
    CHECK_FALSE(analyzer.begin(0, zero_length));
    CHECK(analyzer.done());

    std::vector<StepResponseAnalyzer::Segment> too_many(StepResponseAnalyzer::max_segments + 1, { 1.f, 1.f });
    CHECK_FALSE(analyzer.begin(0, too_many));
}

TEST_CASE("pressure_advance_calibration::StepResponseAnalyzer linear nozzle") {
    const float tau = GENERATE(0.015f, 0.03f, 0.06f);
    const auto nozzle = Nozzle::with_secant(1.f, tau, v_lo, v_hi);
    const auto segments = step_profile(v_lo, v_hi, 0.6f, 0.4f, 6);

    StepResponseAnalyzer analyzer;
    simulate(analyzer, nozzle, 12.5f, segments);

    REQUIRE(analyzer.done());
    REQUIRE(analyzer.results().size() == segments.size());
    CHECK(analyzer.gaps() == 0);

    for (size_t i = 0; i < segments.size(); ++i) {
        const auto &result = analyzer.results()[i];
        CHECK(result.rising == (i % 2 == 0));
        if (tau <= 0.03f) {
            // Slower responses are still settling, which shows up as plateau noise
            CHECK(result.noise < 1.5f);
        }
        if (i >= 2) {
            CHECK_THAT(result.tau_s, WithinRel(tau, 0.05f));

            size_t valid_lags = 0;
            for (const float lag_tau : result.lag_tau_s) {
                if (std::isfinite(lag_tau)) {
                    ++valid_lags;
                    CHECK_THAT(lag_tau, WithinRel(tau, 0.1f));
                }
            }
            CHECK(valid_lags == result.valid_lags);
        }
    }

    const auto summary = summarize(analyzer);
    CHECK_THAT(summary.pressure_advance, WithinRel(tau, 0.03f));

    // Plateau levels follow the flow
    const auto results = analyzer.results();
    CHECK(results[2].level - results[3].level > 1000.f);
}

TEST_CASE("pressure_advance_calibration::StepResponseAnalyzer shear thinning nozzle") {
    // The rising and falling steps settle at different rates; their combination has to land on the
    // secant, which is the pressure advance that cancels the step.
    const float n = GENERATE(0.6f, 0.4f);
    const float secant = GENERATE(0.02f, 0.045f);
    const auto nozzle = Nozzle::with_secant(n, secant, v_lo, v_hi);
    const auto segments = step_profile(v_lo, v_hi, 0.6f, 0.4f, 6);

    StepResponseAnalyzer analyzer;
    simulate(analyzer, nozzle, 0.f, segments);

    const auto summary = summarize(analyzer);
    CHECK(summary.tau_falling > summary.tau_rising);
    CHECK_THAT(summary.pressure_advance, WithinRel(secant, 0.06f));
}

TEST_CASE("pressure_advance_calibration::StepResponseAnalyzer disturbances") {
    const float tau = 0.035f;
    const auto nozzle = Nozzle::with_secant(1.f, tau, v_lo, v_hi);
    const auto segments = step_profile(v_lo, v_hi, 0.6f, 0.4f, 6);

    SimOptions options;
    SECTION("noise") {
        options.noise_g = 8.f;
    }
    SECTION("load cell drift") {
        options.drift_g_per_s = 50.f;
    }
    SECTION("late timestamps") {
        options.timestamp_shift_s = 0.006f;
    }
    SECTION("early timestamps") {
        options.timestamp_shift_s = -0.004f;
    }
    SECTION("dropped samples") {
        options.drop_every = 37;
    }
    SECTION("soft acceleration") {
        options.accel = 500.f;
    }

    StepResponseAnalyzer analyzer;
    simulate(analyzer, nozzle, 3.f, segments, options);

    REQUIRE(analyzer.results().size() == segments.size());
    if (options.drop_every) {
        CHECK(analyzer.gaps() > 0);
    }
    CHECK_THAT(summarize(analyzer).pressure_advance, WithinRel(tau, 0.05f));
}

TEST_CASE("pressure_advance_calibration::StepResponseAnalyzer run boundaries") {
    const auto nozzle = Nozzle::with_secant(1.f, 0.03f, v_lo, v_hi);
    const auto segments = step_profile(v_lo, v_hi, 0.6f, 0.4f, 2);

    SECTION("motor stopping short of the plan end is finished explicitly") {
        StepResponseAnalyzer analyzer;
        SimOptions options;
        options.final_position_error_mm = -0.001f;
        simulate(analyzer, nozzle, 1.f, segments, options);
        CHECK(analyzer.results().size() == segments.size());
    }

    SECTION("motor reaching the plan end finishes on its own") {
        StepResponseAnalyzer analyzer;
        SimOptions options;
        options.final_position_error_mm = 0;
        simulate(analyzer, nozzle, 1.f, segments, options);
        CHECK(analyzer.results().size() == segments.size());
    }

    SECTION("stale positions before the start are ignored") {
        StepResponseAnalyzer analyzer;
        REQUIRE(analyzer.begin(0, segments));
        // Positions from before the extruder position was reset
        analyzer.add_sample(0, 100, 5000.f);
        analyzer.add_sample(3125, 100, 5000.f);
        analyzer.add_sample(6250, 100, NAN);
        CHECK_FALSE(analyzer.done());
        CHECK(analyzer.results().empty());
        CHECK(analyzer.unknown_positions() == 1);
    }

    SECTION("aborted run keeps the finished segments") {
        StepResponseAnalyzer analyzer;
        REQUIRE(analyzer.begin(0, segments));
        uint32_t t = 0;
        float e = 0;
        analyzer.add_sample(t, 0, e);
        // Run through the first segment only
        const float v = segments[0].length_mm / segments[0].duration_s;
        while (e < segments[0].length_mm + 0.1f) {
            t += 3125;
            e += v * 0.003125f;
            analyzer.add_sample(t, 0, e);
        }
        analyzer.finish();
        CHECK(analyzer.done());
        CHECK(analyzer.results().size() == 2);
    }
}
