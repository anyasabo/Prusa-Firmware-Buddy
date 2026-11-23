/// @file
#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace pressure_advance_calibration {

/// Identifies how fast the nozzle pressure follows extruder velocity steps.
///
/// The extruder runs a sequence of constant-velocity segments. The load cell reads the force the
/// filament pushes the hotend with, which is proportional to the nozzle pressure. After each velocity
/// step the force settles towards a new level with a first-order lag. Its time constant is the
/// pressure advance that would make the outflow follow the step.
///
/// The time constant is estimated per segment by regressing the load against itself delayed by a fixed
/// number of samples: for y(t) = F - D * exp(-t / tau), y[i + m] = c + r * y[i] with r = exp(-m * Ts / tau).
/// This needs neither the exact step time nor the final level, so it is immune to the load cell
/// latency and to slow drift of the plateau. Several lags are evaluated and the median is taken.
///
/// Samples are fed one at a time; memory use does not depend on the run length.
///
/// Threading: begin() and the result accessors are called from one context, add_sample() from another
/// (the load cell sample callback). The results may be read once done() returns true, add_sample()
/// does not touch them afterwards.
class StepResponseAnalyzer {
public:
    static constexpr size_t max_segments = 16;

    /// Delays of the regressions [samples]
    static constexpr std::array<uint8_t, 10> lags { 1, 2, 3, 4, 6, 8, 11, 16, 22, 32 };

    using LagTimeConstants = std::array<float, lags.size()>;
    static constexpr LagTimeConstants undetermined_lag_time_constants = [] {
        LagTimeConstants taus {};
        taus.fill(NAN);
        return taus;
    }();

    struct Segment {
        /// Extruder travel during the segment [mm], must be positive
        float length_mm;
        /// Planned duration [s]
        float duration_s;
    };

    struct Config {
        /// Part of each segment right after the step that is ignored [s].
        /// Covers the extruder acceleration ramp and the load cell filter settling.
        float skip_s = 0.010f;
        /// Fraction of the segment (from its start) that is used to fit the time constant;
        /// the rest is the settled plateau
        float transient_fraction = 0.65f;
        /// End of each segment excluded from the plateau [s]. The load cell averages slightly ahead
        /// of its timestamp, so the next step would leak into the last samples.
        float end_guard_s = 0.020f;
        /// A pause between samples longer than this many sample periods is treated as a dropout
        float max_gap_periods = 1.5f;
        /// Nominal sample period of the load cell [s]
        float nominal_period_s = 1.f / 320.f;
    };

    struct SegmentResult {
        /// Time constant of the response to the step at the segment start [s]; NAN when undetermined
        float tau_s = NAN;
        /// Mean load on the settled part of the segment [g]
        float level = NAN;
        /// Standard deviation of the load on the settled part of the segment [g]
        float noise = NAN;
        /// Extruder velocity at the segment start went up
        bool rising = false;
        /// Number of samples used for the time constant fit
        uint16_t fit_samples = 0;
        /// Number of lags that produced a valid time constant estimate
        uint8_t valid_lags = 0;
        /// Time constant estimate of each of the lags [s]; NAN where undetermined.
        /// A first-order response gives the same value for all of them.
        LagTimeConstants lag_tau_s = undetermined_lag_time_constants;
    };

    StepResponseAnalyzer() = default;
    explicit StepResponseAnalyzer(const Config &config)
        : config(config) {}

    /// Prepare for a new run.
    /// @param e_start extruder position at which the first segment starts [mm]
    /// @param segments segments in the order of execution; at most max_segments
    /// @return false if the segments are invalid (the analyzer then stays done())
    bool begin(float e_start, std::span<const Segment> segments);

    /// Feed one load cell sample.
    /// @param time_us sample timestamp
    /// @param load load cell reading [g]; any constant offset is fine
    /// @param e_pos extruder position at the sample time [mm]; NAN when unknown
    void add_sample(uint32_t time_us, float load, float e_pos);

    /// Finalize the running segment when the extruder stopped short of the last segment end
    /// (step rounding) or the run was cut short. Must not run concurrently with add_sample().
    void finish();

    /// All segments have been processed, the run was finished, or begin() failed
    bool done() const { return done_.load(std::memory_order_acquire); }

    /// Number of planned segments
    size_t segment_count() const { return segment_count_; }

    /// Results of the processed segments, in the order of execution; valid once done().
    /// Shorter than segment_count() if the run did not complete.
    std::span<const SegmentResult> results() const { return { results_.data(), finished_segments_ }; }

    /// Number of sample dropouts encountered
    uint16_t gaps() const { return gaps_; }

    /// Number of samples received without a known extruder position
    uint16_t unknown_positions() const { return unknown_positions_; }

private:
    static constexpr size_t history_size = 32;
    static_assert(lags.back() <= history_size);

    /// Least squares sums of y[i + lag] against y[i]
    struct LagSums {
        float sx;
        float sy;
        float sxx;
        float sxy;
        uint16_t n;
    };

    struct SegmentPlan {
        float e_end;
        uint32_t duration_us;
    };

    void start_segment(uint32_t time_us);
    void finish_segment();
    void accumulate(uint32_t time_us, float load);
    void reset_history() { history_count = 0; }

    Config config;

    std::array<SegmentPlan, max_segments> plan_ {};
    std::array<SegmentResult, max_segments> results_ {};
    size_t segment_count_ = 0;
    size_t finished_segments_ = 0;
    float e_start_ = 0;

    /// Index of the running segment; -1 before the extruder starts
    int current = -1;
    /// The extruder was seen standing at the start position; guards against stale positions
    bool start_confirmed = false;
    std::atomic<bool> done_ = true;

    bool have_prev = false;
    uint32_t prev_time_us = 0;
    float prev_e = NAN;

    uint16_t gaps_ = 0;
    uint16_t unknown_positions_ = 0;

    // State of the running segment
    uint32_t segment_start_us = 0;
    std::array<LagSums, lags.size()> lag_sums {};
    std::array<float, history_size> history {};
    size_t history_pos = 0;
    size_t history_count = 0;
    /// Subtracted from the loads of the fit to keep the float sums precise
    float fit_reference = NAN;
    uint16_t fit_samples = 0;
    float period_sum_s = 0;
    uint16_t period_count = 0;
    /// Subtracted from the plateau loads; the first plateau sample, so that the variance does not cancel out
    float plateau_reference = NAN;
    float plateau_sum = 0;
    float plateau_sum_sq = 0;
    uint16_t plateau_n = 0;
};

/// Time constant estimate from a lag regression coefficient.
/// @return NAN if the coefficient carries too little information (too close to 0 or 1)
float tau_from_lag_coefficient(float r, float lag_s);

/// Combine the typical time constants of the rising and falling steps into a pressure advance value.
///
/// For a melt whose pressure grows sub-linearly with the flow rate, the rising step settles faster
/// than the falling one. Pressure advance is linear, so the best single value is the slope of the
/// secant between the two flow rates; the harmonic mean of the two time constants approximates it
/// closely.
float combine_time_constants(float tau_rising_s, float tau_falling_s);

} // namespace pressure_advance_calibration
