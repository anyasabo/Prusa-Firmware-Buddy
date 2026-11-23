/// @file
#include "step_response_analyzer.hpp"

#include <algorithm>

namespace pressure_advance_calibration {

namespace {

    /// Lag coefficients outside this range are dominated by noise or by the plateau
    constexpr float min_lag_coefficient = 0.1f;
    constexpr float max_lag_coefficient = 0.9f;

    /// Minimum number of sample pairs for a lag regression
    constexpr uint16_t min_lag_pairs = 8;

    /// Tolerance for the extruder standing at the start position before the run [mm]
    constexpr float start_position_tolerance_mm = 0.01f;

    /// Tolerance for reaching a segment end, covers the rounding of the position to motor steps [mm]
    constexpr float segment_end_tolerance_mm = 0.0001f;

    int32_t ticks_diff(uint32_t a, uint32_t b) {
        return static_cast<int32_t>(a - b);
    }

    float median(std::span<float> values) {
        if (values.empty()) {
            return NAN;
        }
        std::sort(values.begin(), values.end());
        const size_t mid = values.size() / 2;
        return (values.size() % 2) ? values[mid] : 0.5f * (values[mid - 1] + values[mid]);
    }

} // namespace

float tau_from_lag_coefficient(float r, float lag_s) {
    if (!(r >= min_lag_coefficient && r <= max_lag_coefficient)) {
        return NAN;
    }
    return -lag_s / std::log(r);
}

float combine_time_constants(float tau_rising_s, float tau_falling_s) {
    if (!(tau_rising_s > 0) || !(tau_falling_s > 0)) {
        return NAN;
    }
    return 2.f * tau_rising_s * tau_falling_s / (tau_rising_s + tau_falling_s);
}

bool StepResponseAnalyzer::begin(float e_start, std::span<const Segment> segments) {
    done_.store(true, std::memory_order_relaxed);

    segment_count_ = 0;
    finished_segments_.store(0, std::memory_order_relaxed);
    current = -1;
    start_confirmed = false;
    have_prev = false;
    prev_e = NAN;
    gaps_ = 0;
    unknown_positions_ = 0;
    e_start_ = e_start;

    if (segments.empty() || segments.size() > max_segments) {
        return false;
    }

    float e = e_start;
    float prev_velocity = 0;
    for (size_t i = 0; i < segments.size(); ++i) {
        const Segment &segment = segments[i];
        if (!(segment.length_mm > 0) || !(segment.duration_s > 0)) {
            return false;
        }
        e += segment.length_mm;
        plan_[i] = { .e_end = e, .duration_us = static_cast<uint32_t>(segment.duration_s * 1e6f) };

        const float velocity = segment.length_mm / segment.duration_s;
        results_[i] = { .rising = velocity > prev_velocity };
        prev_velocity = velocity;
    }
    segment_count_ = segments.size();

    done_.store(false, std::memory_order_release);
    return true;
}

void StepResponseAnalyzer::add_sample(uint32_t time_us, float load, float e_pos) {
    if (done_.load(std::memory_order_acquire)) {
        return;
    }

    const bool position_known = std::isfinite(e_pos);
    if (!position_known) {
        ++unknown_positions_;
    }

    if (have_prev && ticks_diff(time_us, prev_time_us) > static_cast<int32_t>(config.max_gap_periods * config.nominal_period_s * 1e6f)) {
        ++gaps_;
        reset_history();
    }

    if (position_known) {
        if (!start_confirmed) {
            // Positions recorded before the extruder position was reset may still arrive;
            // only start tracking once the extruder is seen at the start.
            start_confirmed = std::abs(e_pos - e_start_) <= start_position_tolerance_mm;
        } else if (current < 0) {
            if (e_pos > e_start_ + start_position_tolerance_mm) {
                current = 0;
                start_segment(time_us);
            }
        } else {
            // Several boundaries can be crossed between two samples only with a broken plan,
            // but handle it gracefully anyway.
            while (current < static_cast<int>(segment_count_) && e_pos >= plan_[current].e_end - segment_end_tolerance_mm) {
                uint32_t crossing_us = time_us;
                if (have_prev && std::isfinite(prev_e) && e_pos > prev_e) {
                    const float fraction = std::clamp((plan_[current].e_end - prev_e) / (e_pos - prev_e), 0.f, 1.f);
                    crossing_us = prev_time_us + static_cast<uint32_t>(fraction * static_cast<float>(ticks_diff(time_us, prev_time_us)));
                }
                finish_segment();
                ++current;
                if (current == static_cast<int>(segment_count_)) {
                    done_.store(true, std::memory_order_release);
                    return;
                }
                start_segment(crossing_us);
            }
        }
    }

    if (current >= 0) {
        accumulate(time_us, load);
    }

    have_prev = true;
    prev_time_us = time_us;
    prev_e = e_pos;
}

void StepResponseAnalyzer::finish() {
    if (done_.load(std::memory_order_acquire)) {
        return;
    }
    if (current >= 0) {
        finish_segment();
    }
    done_.store(true, std::memory_order_release);
}

void StepResponseAnalyzer::start_segment(uint32_t time_us) {
    segment_start_us = time_us;
    lag_sums = {};
    reset_history();
    fit_reference = NAN;
    fit_samples = 0;
    period_sum_s = 0;
    period_count = 0;
    plateau_reference = NAN;
    plateau_sum = 0;
    plateau_sum_sq = 0;
    plateau_n = 0;
}

void StepResponseAnalyzer::accumulate(uint32_t time_us, float load) {
    const SegmentPlan &segment = plan_[current];
    const int32_t since_start_us = ticks_diff(time_us, segment_start_us);
    const int32_t fit_start_us = static_cast<int32_t>(config.skip_s * 1e6f);
    const int32_t fit_end_us = static_cast<int32_t>(config.transient_fraction * static_cast<float>(segment.duration_us));

    if (since_start_us < fit_start_us) {
        return;
    }

    if (since_start_us > fit_end_us) {
        // Settled part of the segment
        const int32_t plateau_end_us = static_cast<int32_t>(segment.duration_us) - static_cast<int32_t>(config.end_guard_s * 1e6f);
        if (since_start_us > plateau_end_us) {
            return;
        }
        if (std::isnan(plateau_reference)) {
            plateau_reference = load;
        }
        const float x = load - plateau_reference;
        plateau_sum += x;
        plateau_sum_sq += x * x;
        ++plateau_n;
        return;
    }

    if (std::isnan(fit_reference)) {
        fit_reference = load;
    }
    const float y = load - fit_reference;

    if (history_count > 0) {
        const float period_s = static_cast<float>(ticks_diff(time_us, prev_time_us)) * 1e-6f;
        period_sum_s += period_s;
        ++period_count;
    }

    for (size_t i = 0; i < lags.size(); ++i) {
        const size_t lag = lags[i];
        if (history_count < lag) {
            break;
        }
        const float x = history[(history_pos + history_size - lag) % history_size];
        LagSums &sums = lag_sums[i];
        sums.sx += x;
        sums.sy += y;
        sums.sxx += x * x;
        sums.sxy += x * y;
        ++sums.n;
    }

    history[history_pos] = y;
    history_pos = (history_pos + 1) % history_size;
    history_count = std::min(history_count + 1, history_size);
    ++fit_samples;
}

void StepResponseAnalyzer::finish_segment() {
    SegmentResult &result = results_[current];
    result.fit_samples = fit_samples;

    if (plateau_n > 1) {
        const float mean = plateau_sum / plateau_n;
        result.level = plateau_reference + mean;
        result.noise = std::sqrt(std::max(0.f, plateau_sum_sq / plateau_n - mean * mean));
    }

    if (period_count > 0) {
        fit_time_constant(result);
    }
    finished_segments_.store(current + 1, std::memory_order_release);
}

void StepResponseAnalyzer::fit_time_constant(SegmentResult &result) const {
    const float period_s = period_sum_s / period_count;

    std::array<float, lags.size()> taus;
    size_t tau_count = 0;
    for (size_t i = 0; i < lags.size(); ++i) {
        const LagSums &sums = lag_sums[i];
        if (sums.n < min_lag_pairs) {
            continue;
        }
        const float n = sums.n;
        const float sxx = sums.sxx - sums.sx * sums.sx / n;
        const float sxy = sums.sxy - sums.sx * sums.sy / n;
        if (!(sxx > 0)) {
            continue;
        }
        const float tau = tau_from_lag_coefficient(sxy / sxx, lags[i] * period_s);
        result.lag_tau_s[i] = tau;
        if (std::isfinite(tau)) {
            taus[tau_count++] = tau;
        }
    }

    result.valid_lags = tau_count;
    result.tau_s = median({ taus.data(), tau_count });
}

} // namespace pressure_advance_calibration
