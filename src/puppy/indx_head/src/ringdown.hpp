/// @file
/// Analysis of the LC ringdown of the induction heater, without hardware access. heater.cpp
/// captures the samples and applies the result.
#pragma once

#include <cstddef>
#include <cstdint>
#include <indx_head/nozzle_presence.hpp>
#include <span>

namespace indx_head::heater {

enum class RingdownAnalysisStatus {
    VALID = 0,
    NOT_ENOUGH_PEAKS,
    INTERVAL_TOO_SHORT,
    INTERVAL_TOO_LONG,
    INTERVAL_CHANGE_TOO_BIG,
};

struct RingdownAnalysis {
    RingdownAnalysisStatus status;
    uint16_t interval; // in CPU cycles
    float decay;
    indx_head::NozzlePresence nozzle_presence;
    uint32_t time; // in sys_tick
};

inline constexpr size_t sample_buffer_len = 256;
inline constexpr uint16_t adc_sample_cycles = 14; // 12.5 + 1.5, divider set to 1
inline constexpr uint8_t avg_peaks = 3; // number of ring-down cycles to average

// Decay-to-presence thresholds. Reference measurements: ~0.096 with a fully seated nozzle,
// ~0.029 without a nozzle. Values strictly between the thresholds indicate partial coupling
// (e.g. nozzle stuck halfway) and are reported as `unknown` so the debouncer never settles
// on them. Only `present` is safe to heat — below the present threshold the LC circuit has
// no/insufficient energy sink and would burn the TVS/MOSFET at full power.
inline constexpr float nozzle_present_decay_threshold = 0.085f;
inline constexpr float nozzle_absent_decay_threshold = 0.04f;

using Samples = std::span<const volatile uint16_t, sample_buffer_len>;

/// Nozzle presence for a ringdown decay. A NaN decay is `unknown`.
indx_head::NozzlePresence classify_decay(float decay);

/// Mean of the samples: the DC offset of the ADC input with no oscillation.
uint16_t dc_offset(Samples samples);

/// Find the zero crossings and the peaks of the ringdown in the samples.
/// The status is VALID or NOT_ENOUGH_PEAKS. With NOT_ENOUGH_PEAKS, the other fields are 0
/// and the presence is `unknown`. The time is 0: the caller sets it.
RingdownAnalysis analyze_ringdown(Samples samples, uint16_t dc_offset);

/// Check the interval of a VALID analysis against the absolute limits and against
/// last_valid_interval (0: no previous value). On failure, set the status and return false.
/// Update last_valid_interval.
bool ringdown_analysis_sanity_check(RingdownAnalysis &result, uint16_t &last_valid_interval);

/// Return true if the heater must measure the LC response again. The parameters drift, and
/// the measurement also checks the nozzle presence.
bool should_measure(const RingdownAnalysis &last_analysis, uint32_t now_tick, bool idle);

} // namespace indx_head::heater
