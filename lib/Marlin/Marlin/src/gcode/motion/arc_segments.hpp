#pragma once

#include <cstdint>

/// Upper limit of arc_segment_count(): 1677 m of arc at 0.1 mm segments.
/// A float can hold each integer up to this value.
inline constexpr uint32_t arc_segment_count_max = uint32_t(1) << 24;

/// Number of line segments for an arc of flat_mm length, with segments of about segment_mm.
/// \returns a value in 1 ... arc_segment_count_max, also for NaN and infinite arguments
inline uint32_t arc_segment_count(float flat_mm, float segment_mm) {
    const float count = flat_mm / segment_mm + 0.8f;
    // A cast to uint32_t of NaN or of a value out of range is undefined.
    // NaN fails each comparison, so the first check also catches it.
    if (!(count >= 1.f)) {
        return 1;
    }
    if (count >= float(arc_segment_count_max)) {
        return arc_segment_count_max;
    }
    return uint32_t(count);
}
