/// @file
#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

namespace buddy {

/// The count, the smallest and the largest raw load cell value of a time window.
///
/// Uses:
/// - Liveness: Z homing trusts the load cell to stop the bed. If the wires between the bridge and
///   the HX717 separate, the open input still streams values near zero with noise like a bridge,
///   and the tare before Z homing hides the change of the offset. Then the bed pushes into the
///   nozzle until the Z motors skip. A connected load cell feels the head motion (the XY homing
///   bumps the frame), an open input does not. The spread during the XY homing tells them apart.
/// - Health metric: the raw level and the noise of each second. A separated bridge reads near zero,
///   a connected one far from zero (about -1.1M counts on a CORE One L).
///
/// feed() runs in the sample ISR. start() and stop() run in one task. The MCU has one core,
/// so feed() always runs to its end before the task continues: start() never sees half an update.
class LoadcellRawWindow {
public:
    struct Result {
        uint32_t samples; ///< samples in the window
        int32_t min; ///< smallest raw value, 0 if no sample
        int32_t max; ///< largest raw value, 0 if no sample

        /// The HX717 gives 24-bit values, so the difference fits in int32_t.
        int32_t spread() const { return max - min; }
    };

    /// Start a new window. Clears the values of the previous window.
    void start() {
        active.store(false, std::memory_order_relaxed);
        samples.store(0, std::memory_order_relaxed);
        min_raw.store(std::numeric_limits<int32_t>::max(), std::memory_order_relaxed);
        max_raw.store(std::numeric_limits<int32_t>::min(), std::memory_order_relaxed);
        active.store(true, std::memory_order_relaxed);
    }

    /// Record a valid raw sample. Call only from the sample ISR (one writer).
    void feed(int32_t raw) {
        if (!active.load(std::memory_order_relaxed)) {
            return;
        }
        samples.store(samples.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        if (raw < min_raw.load(std::memory_order_relaxed)) {
            min_raw.store(raw, std::memory_order_relaxed);
        }
        if (raw > max_raw.load(std::memory_order_relaxed)) {
            max_raw.store(raw, std::memory_order_relaxed);
        }
    }

    /// End the window.
    /// @returns the result of the window, or no samples if start() was not called
    Result stop() {
        const bool was_active = active.exchange(false, std::memory_order_relaxed);
        const uint32_t n = samples.load(std::memory_order_relaxed);
        if (!was_active || n == 0) {
            return { .samples = 0, .min = 0, .max = 0 };
        }
        return { .samples = n, .min = min_raw.load(std::memory_order_relaxed), .max = max_raw.load(std::memory_order_relaxed) };
    }

    bool is_active() const { return active.load(std::memory_order_relaxed); }

private:
    static_assert(std::atomic<uint32_t>::is_always_lock_free, "Lock free type must be used from ISR.");
    static_assert(std::atomic<int32_t>::is_always_lock_free, "Lock free type must be used from ISR.");
    std::atomic<bool> active { false };
    std::atomic<uint32_t> samples { 0 };
    std::atomic<int32_t> min_raw { std::numeric_limits<int32_t>::max() };
    std::atomic<int32_t> max_raw { std::numeric_limits<int32_t>::min() };
};

} // namespace buddy
