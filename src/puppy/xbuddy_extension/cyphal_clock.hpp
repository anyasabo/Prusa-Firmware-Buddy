/// @file
#pragma once

#include <canard.h>
#include <cstdint>

namespace cyphal {

/// Monotonic time in microseconds for libcanard, made from a 32-bit millisecond count.
///
/// libcanard compares the 64-bit timestamps directly, so the time must not wrap.
/// The 32-bit millisecond count wraps after 49.7 days. This class counts the wraps.
/// Call update() at least once in each wrap period, and always from the same task.
class MonotonicClock {
public:
    CanardMicrosecond update(uint32_t now_ms) {
        if (now_ms < last_ms) {
            ++wraps;
        }
        last_ms = now_ms;
        return ((static_cast<uint64_t>(wraps) << 32) | now_ms) * 1000;
    }

private:
    uint32_t last_ms = 0;
    uint32_t wraps = 0;
};

} // namespace cyphal
