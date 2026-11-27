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
    // NaN fails the comparison
    return range.frequency_increment >= min_frequency_increment;
}

} // namespace vibrate_measure
