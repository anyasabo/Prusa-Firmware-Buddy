/// @file
/// Parameters of M593 and the pure functions that turn them into an input shaper configuration.
/// They are in a header so that the unit tests can use them without Marlin.
#pragma once

#include <feature/input_shaper/input_shaper_config.hpp>

#include <optional>

namespace input_shaper {

struct M593Params {
    bool seen_x;
    bool seen_y;
    bool seen_z;
    bool seen_w;
    struct {
        std::optional<Type> type;
        std::optional<float> frequency;
        std::optional<float> damping_ratio;
        std::optional<float> vibration_reduction;
    } axis;
    struct {
        std::optional<float> frequency_delta;
        std::optional<float> mass_limit;
    } weight_adjust;
};

/// Returns the axis configuration after M593: each parameter that M593 does not give keeps its value from @p config.
/// Frequency 0 disables the input shaper of the axis.
inline std::optional<AxisConfig> get_axis_config(const AxisConfig &config, const M593Params &params) {
    if (params.axis.frequency && *params.axis.frequency == 0) {
        return std::nullopt;
    }
    return AxisConfig {
        .type = params.axis.type.value_or(config.type),
        .frequency = params.axis.frequency.value_or(config.frequency),
        .damping_ratio = params.axis.damping_ratio.value_or(config.damping_ratio),
        .vibration_reduction = params.axis.vibration_reduction.value_or(config.vibration_reduction),
    };
}

/// Clamps the frequency of @p params to the safe range. Sets @p clamped if the frequency changed.
inline M593Params clamp_frequency(M593Params params, bool &clamped) {
    clamped = false;
    // `optional != 0.` is also true for an empty optional, so check that the frequency is given.
    if (params.axis.frequency && *params.axis.frequency != 0.f) {
        const float original_frequency = *params.axis.frequency;
        const float clamped_frequency = clamp_frequency_to_safe_values(original_frequency);
        if (clamped_frequency != original_frequency) {
            clamped = true;
            params.axis.frequency = clamped_frequency;
        }
    }
    return params;
}

} // namespace input_shaper
