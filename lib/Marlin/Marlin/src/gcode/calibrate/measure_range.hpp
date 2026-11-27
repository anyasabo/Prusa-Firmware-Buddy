#pragma once

namespace vibrate_measure {

struct MeasureRange {
    float start_frequency;
    float end_frequency;
    float frequency_increment;
};

/// Smallest frequency step (Hz) of M959. It is equal to the tolerance of the
/// frequency loops in M958.cpp. A smaller step makes the frequency resolution
/// finer than that tolerance, and a step of 0 makes the loops endless.
inline constexpr float min_frequency_increment = 0.01f;

/// \returns true if the frequency loops over \p range can end
constexpr bool is_frequency_increment_valid(const MeasureRange &range) {
    // The loops end at end_frequency + 0.01. If half the step changes this
    // largest frequency, the step is at least one float spacing there. The
    // spacing only gets smaller for smaller values, so then each addition in
    // the loops changes the frequency. NaN fails each comparison.
    const float last_frequency = range.end_frequency + 0.01f;
    return range.frequency_increment >= min_frequency_increment
        && last_frequency + 0.5f * range.frequency_increment > last_frequency;
}

} // namespace vibrate_measure
