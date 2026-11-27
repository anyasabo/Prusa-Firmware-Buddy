#pragma once

#include <climits>
#include <numbers>

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

/// Displacement amplitude (m) of a harmonic excitation at \p frequency (Hz) and
/// \p acceleration (m/s^2): the double integral of the acceleration over time
constexpr float excitation_amplitude(float frequency, float acceleration) {
    return acceleration / (4 * std::numbers::pi_v<float> * std::numbers::pi_v<float> * frequency * frequency);
}

/// \returns true if M958 can excite \p amplitude (m) with steps of \p step_len (m)
///
/// The step generator rounds the amplitude up to whole steps and makes 4 times
/// that many steps in a period, as an int. With 0 steps (acceleration 0) the
/// step loop takes the step number modulo 0. An infinite amplitude
/// (frequency 0) or one with too many steps overflows the int. NaN fails each
/// comparison.
constexpr bool is_excitation_amplitude_valid(float amplitude, float step_len) {
    const float steps = amplitude / step_len;
    return steps > 0 && steps <= static_cast<float>(INT_MAX / 4 - 1);
}

} // namespace vibrate_measure
