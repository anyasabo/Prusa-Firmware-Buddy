/// @file
#include "pressure_advance_calibration.hpp"

#include "step_response_analyzer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <span>
#include <tuple>

#include <Marlin/src/Marlin.h>
#include <Marlin/src/feature/pressure_advance/pressure_advance_config.hpp>
#include <Marlin/src/feature/prusa/e-stall_detector.h>
#include <Marlin/src/module/motion.h>
#include <Marlin/src/module/planner.h>
#include <config_store/store_instance.hpp>
#include <filament.hpp>
#include <loadcell.hpp>
#include <logging/log.hpp>
#include <mapi/feedrates/standard_feedrates.hpp>
#include <mapi/motion.hpp>
#include <mapi/parking.hpp>
#include <raii/scope_guard.hpp>
#include <timing.h>
#include <tool/hotend/hotend.hpp>
#include <utils/string_builder.hpp>

#include <option/has_pressure_advance_calibration.h>

static_assert(HAS_PRESSURE_ADVANCE_CALIBRATION());

LOG_COMPONENT_DEF(PressureAdvanceCalibration, logging::Severity::info);

namespace pressure_advance_calibration {

namespace {

    namespace feedrates = buddy::standard_feedrates;

    /// Cross-section of 1.75 mm filament [mm^2]
    constexpr float filament_area_mm2 = 2.4053f;

    /// Flow rates of the test for a 0.4 mm nozzle, scaled with the nozzle diameter.
    /// They span the flows of a typical print, so the result fits the transitions in it.
    constexpr float reference_nozzle_diameter_mm = 0.4f;
    constexpr float low_flow_mm3_s = 1.f;
    constexpr float high_flow_mm3_s = 9.f;
    constexpr float max_high_flow_mm3_s = 14.f;
    constexpr float max_high_flow_hf_nozzle_mm3_s = 20.f;

    /// Durations of the flow rate steps. The load has to settle within each step; the step down to the
    /// low flow settles slower.
    constexpr float prime_duration_s = 0.4f;
    constexpr float high_duration_s = 0.4f;
    constexpr float low_duration_s = 0.6f;
    constexpr int cycles = 6;

    /// Prime, lead-in and the first cycle only bring the melt into a periodic steady state
    constexpr size_t warmup_segments = 4;

    /// Step durations are multiplied by this when the response turned out to be too slow
    constexpr float slow_retry_scale = 2.f;

    /// A time constant above this fraction of the step duration means the step did not settle
    constexpr float max_tau_fraction = 0.22f;

    /// Plausible range of the result [s]
    constexpr float min_pressure_advance = 0.004f;
    constexpr float max_pressure_advance = 0.3f;

    /// Relative spread of the time constants above which the measurement is rejected
    constexpr float max_spread = 0.25f;

    /// The load difference between the flow rates has to exceed both [g] and [plateau noise]
    constexpr float min_load_step_g = 30.f;
    constexpr float min_load_step_to_noise = 15.f;

    /// Minimum number of usable steps in each direction
    constexpr size_t min_steps = 3;

    constexpr uint32_t heating_timeout_ms = 5 * 60 * 1000;

    /// Lets the position lookback refill after the extruder position reset
    constexpr uint32_t position_settle_ms = 50;

    /// The load cell delivers the last samples of the run a little after the motion ends
    constexpr uint32_t sample_latency_ms = 50;

    /// The log file buffer holds only a few lines, the per-segment report is spread out so that it
    /// does not overflow
    constexpr uint32_t log_line_spacing_ms = 50;

    StepResponseAnalyzer analyzer;
    std::atomic<StepResponseAnalyzer *> active_analyzer = nullptr;

    struct Plan {
        std::array<StepResponseAnalyzer::Segment, StepResponseAnalyzer::max_segments> segments;
        size_t count = 0;
        float duration_s = 0;
        float length_mm = 0;
        float high_duration_s = 0;
        float low_duration_s = 0;

        void add(float velocity_mm_s, float duration_s) {
            segments[count++] = { .length_mm = velocity_mm_s * duration_s, .duration_s = duration_s };
            this->duration_s += duration_s;
            length_mm += velocity_mm_s * duration_s;
        }

        std::span<const StepResponseAnalyzer::Segment> span() const { return { segments.data(), count }; }
    };

    Plan make_plan(PhysicalToolIndex tool, float duration_scale) {
        const float nozzle_scale = std::clamp(config_store().get_nozzle_diameter(tool) / reference_nozzle_diameter_mm, 0.5f, 2.5f);
        const float max_high_flow = config_store().get_nozzle_is_high_flow(tool) ? max_high_flow_hf_nozzle_mm3_s : max_high_flow_mm3_s;
        const float low_velocity = low_flow_mm3_s * nozzle_scale / filament_area_mm2;
        const float high_velocity = std::min(high_flow_mm3_s * nozzle_scale, max_high_flow) / filament_area_mm2;

        Plan plan;
        plan.high_duration_s = high_duration_s * duration_scale;
        plan.low_duration_s = low_duration_s * duration_scale;
        plan.add(high_velocity, prime_duration_s);
        plan.add(low_velocity, plan.low_duration_s);
        for (int i = 0; i < cycles; ++i) {
            plan.add(high_velocity, plan.high_duration_s);
            plan.add(low_velocity, plan.low_duration_s);
        }
        static_assert(2 + 2 * cycles <= StepResponseAnalyzer::max_segments);
        return plan;
    }

    void wait_ms(uint32_t ms) {
        const uint32_t start = ticks_ms();
        while (ticks_diff(ticks_ms(), start) < static_cast<int32_t>(ms)) {
            idle(true);
        }
    }

    float median(std::span<float> values) {
        if (values.empty()) {
            return NAN;
        }
        std::sort(values.begin(), values.end());
        const size_t mid = values.size() / 2;
        return (values.size() % 2) ? values[mid] : 0.5f * (values[mid - 1] + values[mid]);
    }

    /// Median absolute deviation relative to the median
    float relative_spread(std::span<const float> values, float center) {
        std::array<float, StepResponseAnalyzer::max_segments> deviations;
        for (size_t i = 0; i < values.size(); ++i) {
            deviations[i] = std::abs(values[i] - center);
        }
        return median({ deviations.data(), values.size() }) / center;
    }

    /// Collected values of one step direction
    struct Steps {
        std::array<float, StepResponseAnalyzer::max_segments> taus;
        size_t tau_count = 0;
        std::array<float, StepResponseAnalyzer::max_segments> load_steps;
        size_t load_step_count = 0;

        std::span<float> tau_span() { return { taus.data(), tau_count }; }
        std::span<float> load_step_span() { return { load_steps.data(), load_step_count }; }
    };

    /// Runs the test moves and feeds the load cell samples to the analyzer
    Error measure(const Plan &plan, const Callbacks &callbacks) {
        planner.synchronize();

        // Measure from a fresh origin: the position counter keeps growing and floats would lose resolution
        current_position.e = 0;
        sync_plan_position_e();
        wait_ms(position_settle_ms);

        if (!analyzer.begin(planner.get_axis_position_mm(E_AXIS), plan.span())) {
            return Error::measurement_failed;
        }
        active_analyzer.store(&analyzer, std::memory_order_release);

        bool queued = true;
        for (const auto &segment : plan.span()) {
            queued = queued && mapi::extruder_move(segment.length_mm, segment.length_mm / segment.duration_s);
        }

        const uint32_t start_ms = ticks_ms();
        const uint32_t timeout_ms = static_cast<uint32_t>(plan.duration_s * 2000.f) + 5000;
        bool timed_out = false;
        while (planner.busy()) {
            const uint32_t elapsed_ms = ticks_ms() - start_ms;
            if (callbacks.on_progress) {
                std::ignore = callbacks.on_progress(Stage::measuring, static_cast<uint8_t>(std::min(99.f, elapsed_ms / (plan.duration_s * 10.f))));
            }
            if (elapsed_ms > timeout_ms) {
                timed_out = true;
                planner.quick_stop_and_resume();
                break;
            }
            idle(true);
        }
        planner.synchronize();

        wait_ms(sample_latency_ms);
        active_analyzer.store(nullptr, std::memory_order_release);
        // Make sure no sample is being processed anymore
        loadcell.WaitBarrier();
        analyzer.finish();

        log_info(PressureAdvanceCalibration, "samples: gaps %u, unknown positions %u", analyzer.gaps(), analyzer.unknown_positions());

        if (!queued || timed_out || analyzer.results().size() != plan.count) {
            log_error(PressureAdvanceCalibration, "incomplete run (queued %d, timeout %d, segments %u/%u)", queued, timed_out, analyzer.results().size(), plan.count);
            return Error::measurement_failed;
        }
        return Error::none;
    }

    Result evaluate(const Plan &plan) {
        const auto segments = analyzer.results();

        Steps rising, falling;
        std::array<float, StepResponseAnalyzer::max_segments> noises;
        size_t noise_count = 0;

        for (size_t i = 0; i < segments.size(); ++i) {
            const auto &segment = segments[i];
            ArrayStringBuilder<StepResponseAnalyzer::lags.size() * 7 + 1> lag_taus;
            for (const float tau : segment.lag_tau_s) {
                lag_taus.append_printf(" %.1f", static_cast<double>(tau * 1000.f));
            }
            log_info(PressureAdvanceCalibration, "segment %u %s: tau %.4f s, level %.1f g, noise %.2f g, fit %u, lags [ms]%s",
                i, segment.rising ? "up" : "down", static_cast<double>(segment.tau_s), static_cast<double>(segment.level),
                static_cast<double>(segment.noise), segment.fit_samples, lag_taus.str_nocheck());
            wait_ms(log_line_spacing_ms);

            if (i < warmup_segments) {
                continue;
            }
            Steps &steps = segment.rising ? rising : falling;
            if (std::isfinite(segment.tau_s)) {
                steps.taus[steps.tau_count++] = segment.tau_s;
            }
            const float load_step = segment.level - segments[i - 1].level;
            if (std::isfinite(load_step)) {
                steps.load_steps[steps.load_step_count++] = load_step;
            }
            if (std::isfinite(segment.noise)) {
                noises[noise_count++] = segment.noise;
            }
        }

        Result result;

        // The load has to follow the flow: up on the rising steps, down on the falling ones
        // (or the other way around, depending on the load cell orientation)
        const float load_step_rising = median(rising.load_step_span());
        const float load_step_falling = median(falling.load_step_span());
        const float noise = median({ noises.data(), noise_count });
        result.load_step = 0.5f * std::abs(load_step_rising - load_step_falling);
        const float min_load_step = std::max(min_load_step_g, min_load_step_to_noise * noise);
        log_info(PressureAdvanceCalibration, "load step up %.1f g, down %.1f g, noise %.2f g",
            static_cast<double>(load_step_rising), static_cast<double>(load_step_falling), static_cast<double>(noise));
        wait_ms(log_line_spacing_ms);
        if (!(std::abs(load_step_rising) >= min_load_step && std::abs(load_step_falling) >= min_load_step)
            || std::signbit(load_step_rising) == std::signbit(load_step_falling)) {
            result.error = Error::no_signal;
            return result;
        }

        if (rising.tau_count < min_steps || falling.tau_count < min_steps) {
            result.error = Error::inconsistent;
            return result;
        }

        result.tau_rising = median(rising.tau_span());
        result.tau_falling = median(falling.tau_span());
        result.spread = std::max(relative_spread(rising.tau_span(), result.tau_rising), relative_spread(falling.tau_span(), result.tau_falling));
        result.pressure_advance = combine_time_constants(result.tau_rising, result.tau_falling);
        log_info(PressureAdvanceCalibration, "tau up %.4f s, down %.4f s, spread %.2f, pressure advance %.4f s",
            static_cast<double>(result.tau_rising), static_cast<double>(result.tau_falling), static_cast<double>(result.spread),
            static_cast<double>(result.pressure_advance));
        wait_ms(log_line_spacing_ms);

        if (result.spread > max_spread) {
            result.error = Error::inconsistent;
        } else if (result.tau_rising > max_tau_fraction * plan.high_duration_s || result.tau_falling > max_tau_fraction * plan.low_duration_s) {
            result.error = Error::too_slow;
        } else if (!(result.pressure_advance >= min_pressure_advance && result.pressure_advance <= max_pressure_advance)) {
            result.error = Error::out_of_range;
        } else {
            result.pressure_advance = std::round(result.pressure_advance * 1000.f) / 1000.f;
        }
        return result;
    }

    Result calibrate(PhysicalToolIndex tool, const FilamentTypeParameters &filament, const Callbacks &callbacks) {
        // Without the smoothing of pressure advance, the extruder steps are sharp
        pressure_advance::PressureAdvanceDisabler pressure_advance_disabler;
        // The load steps would look like a stall
        BlockEStallDetection block_e_stall_detection;
        // Samples the load cell without interruptions (no channel switching to the filament sensor)
        Loadcell::HighPrecisionEnabler high_precision(loadcell);

        const float original_e = current_position.e;
        ScopeGuard restore_e_position([&] {
            planner.synchronize();
            current_position.e = original_e;
            sync_plan_position_e();
        });

        mapi::fully_deretract(feedrates::extruder(feedrates::Extruder::deretract, filament));

        Result result;
        for (const float duration_scale : { 1.f, slow_retry_scale }) {
            const Plan plan = make_plan(tool, duration_scale);
            log_info(PressureAdvanceCalibration, "run: %u segments, %.1f s, %.1f mm", plan.count, static_cast<double>(plan.duration_s), static_cast<double>(plan.length_mm));

            if (const Error error = measure(plan, callbacks); error != Error::none) {
                result.error = error;
                break;
            }
            result = evaluate(plan);
            if (result.error != Error::too_slow) {
                break;
            }
            log_info(PressureAdvanceCalibration, "response too slow, retrying with longer steps");
        }

        mapi::retract_to(STANDARD_RETRACT_LENGTH, feedrates::extruder(feedrates::Extruder::retract, filament));
        return result;
    }

} // namespace

void on_loadcell_sample(uint32_t time_us, float load, float e_pos) {
    if (StepResponseAnalyzer *const active = active_analyzer.load(std::memory_order_acquire)) {
        active->add_sample(time_us, load, e_pos);
    }
}

const char *to_string(Error error) {
    switch (error) {
    case Error::none:
        return "none";
    case Error::no_filament:
        return "no filament loaded";
    case Error::flexible_filament:
        return "flexible filaments are not supported";
    case Error::aborted:
        return "aborted";
    case Error::heating_failed:
        return "nozzle did not reach the temperature";
    case Error::measurement_failed:
        return "measurement failed";
    case Error::no_signal:
        return "load does not follow the flow";
    case Error::inconsistent:
        return "inconsistent measurements";
    case Error::too_slow:
        return "pressure settles too slowly";
    case Error::out_of_range:
        return "result out of range";
    }
    return "unknown";
}

Result run(const Callbacks &callbacks) {
    const auto report = [&](Stage stage, uint8_t percent) {
        return !callbacks.on_progress || callbacks.on_progress(stage, percent);
    };

    const auto tool = PhysicalToolIndex::currently_selected_opt();
    const auto virtual_tool = VirtualToolIndex::currently_selected_opt();
    if (!tool || !virtual_tool) {
        return { .error = Error::no_filament };
    }
    const FilamentType filament_type = FilamentType::for_tool(*virtual_tool);
    if (filament_type == FilamentType::none) {
        return { .error = Error::no_filament };
    }
    const FilamentTypeParameters filament = filament_type.parameters();
    if (filament.is_flexible) {
        return { .error = Error::flexible_filament };
    }

    if (!report(Stage::preparing, 0)) {
        return { .error = Error::aborted };
    }

    // Extrude where the filament load purges
    mapi::home_if_needed_and_park(mapi::get_parking_position(mapi::ParkPosition::purge));
    planner.synchronize();

    // Measure at the print temperature of the filament, the melt viscosity depends on it
    Hotend &hotend = Hotend::for_tool(*tool);
    const auto original_target = hotend.nozzle_target_temp();
    ScopeGuard restore_temperature([&] {
        hotend.set_nozzle_target_temp(original_target);
    });
    hotend.set_nozzle_target_temp(filament.nozzle_temperature);

    const uint32_t heating_start_ms = ticks_ms();
    while (!hotend.is_nozzle_temp_reached()) {
        if (!report(Stage::preparing, 0)) {
            return { .error = Error::aborted };
        }
        if (ticks_ms() - heating_start_ms > heating_timeout_ms) {
            return { .error = Error::heating_failed };
        }
        idle(true);
    }

    if (!report(Stage::measuring, 0)) {
        return { .error = Error::aborted };
    }

    const Result result = calibrate(*tool, filament, callbacks);
    if (result.error == Error::none) {
        log_info(PressureAdvanceCalibration, "result: %.3f s", static_cast<double>(result.pressure_advance));
    } else {
        log_warning(PressureAdvanceCalibration, "failed: %s", to_string(result.error));
    }
    return result;
}

} // namespace pressure_advance_calibration
