/// @file
/// Pure helpers for the load cell probe sequence in run_z_probe() (probe.cpp).
/// They have no dependency on Marlin, so the host unit tests can check them.
#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>

namespace buddy::probe {

/// Time to wait after touchdown so that the analysis window has the dwell it was trained on.
/// @param elapsed_ms time already spent since the end of the move down
/// @param required_ms dwell the analysis expects (Loadcell::TOUCHDOWN_DELAY_MS)
constexpr uint32_t touchdown_remaining_delay_ms(uint32_t elapsed_ms, uint32_t required_ms) {
    return elapsed_ms < required_ms ? required_ms - elapsed_ms : 0;
}

struct XYOffset {
    float x;
    float y;
};

/// XY offset of try @p try_idx at one probe point. Try 0 is at the point. Later tries go on
/// rings 2 mm apart, with about 2 mm between tries on one ring (1, 7, 13, 19, ... tries).
inline XYOffset offset_for_probe_try(int try_idx) {
    constexpr float distance = 2.0f;
    float radius = 0;
    int idx_offset = 0;

    while (true) {
        const float perimeter = 2 * radius * std::numbers::pi_v<float>;
        const int tries_within_perimeter = static_cast<int>(perimeter / distance) + 1;
        if (try_idx < idx_offset + tries_within_perimeter) {
            const int try_within_perimeter = try_idx - idx_offset;
            const float angle = (static_cast<float>(try_within_perimeter) / static_cast<float>(tries_within_perimeter)) * 2 * std::numbers::pi_v<float>;
            return { std::cos(angle) * radius, std::sin(angle) * radius };
        }
        idx_offset += tries_within_perimeter;
        radius += distance;
    }
}

} // namespace buddy::probe
