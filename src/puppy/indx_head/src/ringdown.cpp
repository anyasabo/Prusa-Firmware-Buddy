/// @file
#include "ringdown.hpp"

namespace indx_head::heater {

indx_head::NozzlePresence classify_decay(float decay) {
    // Below the present threshold the LC circuit has no/insufficient energy sink and would
    // burn the TVS/MOSFET if we kept driving it. Decay in the band between thresholds means
    // partial coupling (e.g. nozzle stuck halfway) — reported as `unknown` so the publish
    // call skips it and the debouncer doesn't advance toward a misleading stable state.
    if (decay >= nozzle_present_decay_threshold) {
        return indx_head::NozzlePresence::present;
    } else if (decay < nozzle_absent_decay_threshold) {
        return indx_head::NozzlePresence::absent;
    } else {
        return indx_head::NozzlePresence::unknown;
    }
}

uint16_t dc_offset(Samples samples) {
    // TODO - is this even necesary? The offset seems to be always the same
    uint32_t sum = 0;
    for (size_t i = 0; i < samples.size(); i++) {
        sum += samples[i];
    }
    return sum / samples.size();
}

RingdownAnalysis analyze_ringdown(Samples sample_buffer, uint16_t dcOffset) {
    const uint8_t nulls_cnt = 10;
    uint16_t nulls[nulls_cnt]; // zero-crossing positions in CPU cycles
    uint8_t nulls_index = 0;

    const uint8_t peaks_cnt = nulls_cnt;
    uint16_t peaks[peaks_cnt];
    uint8_t peaks_index = 0;

    bool start_noise = true;

    for (int i = 1; i < static_cast<int>(sample_buffer_len) - 1; i++) {
        // wait for actual oscillations
        if (start_noise) {
            if (sample_buffer[i] > dcOffset - 200) {
                continue;
            } else {
                start_noise = false;
            }
        }
        // find zero crossings to determine frequency
        if (nulls_index < nulls_cnt && sample_buffer[i] <= dcOffset && sample_buffer[i + 1] > dcOffset) {
            // Linear-interpolate the zero crossing with sub-sample precision, but stay
            // in integer math. Resolution is one MCU cycle.
            nulls[nulls_index] = i * adc_sample_cycles
                + (dcOffset - sample_buffer[i]) * adc_sample_cycles / (sample_buffer[i + 1] - sample_buffer[i]);
            nulls_index++;
        }

        // find peak maximums to determine decay
        if (nulls_index > 0 && peaks_index < peaks_cnt) {
            int16_t slope1 = sample_buffer[i] - sample_buffer[i - 1];
            int16_t slope2 = sample_buffer[i + 1] - sample_buffer[i];
            if (slope1 >= 0 && slope2 < 0) {
                peaks[peaks_index] = sample_buffer[i];
                peaks_index++;
            }
        }

        if (nulls_index >= nulls_cnt && peaks_index >= peaks_cnt) {
            break;
        }
    }

    RingdownAnalysis analysis {
        .status = RingdownAnalysisStatus::NOT_ENOUGH_PEAKS,
        .interval = 0,
        .decay = 0,
        .nozzle_presence = indx_head::NozzlePresence::unknown,
        .time = 0,
    };

    if (nulls_index < nulls_cnt || peaks_index < peaks_cnt) {
        return analysis; // invalid data, can't continue
    }
    analysis.status = RingdownAnalysisStatus::VALID;

    // sometime the first peak is distorted.
    uint8_t skip_count = 0;
    while (skip_count < nulls_cnt - (avg_peaks + 1)) {
        if (peaks[skip_count] < peaks[skip_count + 1]) { // first peak cant be smaller than second
            skip_count++;
            continue;
        }
        if (peaks[skip_count] >= dcOffset * 2) { // first peak still under power, wait for decay oscillations
            skip_count++;
            continue;
        }
        break; // all check valid
    }

    float decay = 0;
    for (int i = skip_count; i < skip_count + avg_peaks; i++) {
        decay += (float)peaks[i + 1] / peaks[i];
    }
    decay = 1.0f - (decay / avg_peaks);
    analysis.decay = decay;
    analysis.nozzle_presence = classify_decay(decay);

    uint16_t interval = 0;
    for (int i = skip_count; i < skip_count + avg_peaks; i++) {
        interval += nulls[i + 1] - nulls[i];
    }

    interval = interval / avg_peaks; // interval in CPU cycles
    analysis.interval = interval;
    return analysis;
}

bool ringdown_analysis_sanity_check(RingdownAnalysis &result, uint16_t &last_valid_interval) {
    // TODO - this is very benevolent check
    if (result.interval < 170) {
        result.status = RingdownAnalysisStatus::INTERVAL_TOO_SHORT;
        return false;
    }
    if (result.interval > 300) {
        result.status = RingdownAnalysisStatus::INTERVAL_TOO_LONG;
        return false;
    }

    // Consistency check: interval shouldn't change dramatically between measurements
    if (last_valid_interval > 0) {
        uint16_t diff = result.interval > last_valid_interval ? result.interval - last_valid_interval : last_valid_interval - result.interval;
        if (diff * 5 > last_valid_interval) { // deviation > 20% - INDX_TODO: 20% is too much, needs revision
            last_valid_interval = 0; // Really should we zero it on first bad measurement? This disables this check for next measurement, which could be bad as well.
            result.status = RingdownAnalysisStatus::INTERVAL_CHANGE_TOO_BIG;
            return false;
        }
    }

    last_valid_interval = result.interval;
    return true;
}

bool should_measure(const RingdownAnalysis &last_analysis, uint32_t now_tick, bool idle) {
    if (last_analysis.status == RingdownAnalysisStatus::VALID) {
        // When heating, we don't want to lose power to measurement.
        // It is safe to heat with full power even for 0.5 seconds.
        // In idle, we want to minimize time to missing nozzle detection.
        const uint32_t period = idle ? 100 : 500;

        return now_tick - last_analysis.time > period;
    } else {
        return true;
    }
}

} // namespace indx_head::heater
