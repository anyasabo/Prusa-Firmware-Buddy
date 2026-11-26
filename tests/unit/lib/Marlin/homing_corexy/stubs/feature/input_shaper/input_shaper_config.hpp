#pragma once
#include <core/types.h>
#include <optional>
namespace input_shaper {
struct AxisConfig {
    float frequency;
};
std::optional<AxisConfig> get_axis_config(AxisEnum axis);
void set_axis_config(AxisEnum axis, std::optional<AxisConfig> config);
} // namespace input_shaper
