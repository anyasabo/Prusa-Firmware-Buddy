#pragma once

#include <utils/atomic_circular_queue.hpp>

#include <array>
#include <atomic>
#include <variant>
#include <cstdint>

namespace phase_stepping {
struct Stalled {
    uint32_t timestamp;
    char axis;
    uint32_t number_of_iteration;
};
struct SuddenSpeedChange {
    uint32_t timestamp;
    char axis;
    float original_speed;
    float new_speed;
};
using ProblemEvents = std::variant<Stalled, SuddenSpeedChange>;

/// @brief Queue for debug events
extern AtomicCircularQueue<ProblemEvents, uint16_t, 32> debug_events_queue;

/// Cumulative counts of the problem events of one axis since boot. The queue above holds only 32
/// events, and the metrics send them only during a print. These counts lose no event: the phase
/// stepping ISR counts before it enqueues. The ISR is the only writer, so a relaxed load and store
/// is enough. Readers read each 32-bit value atomically.
struct ProblemCounts {
    std::atomic<uint32_t> stalls; ///< Stalled events
    std::atomic<uint32_t> stalled_refreshes; ///< Sum of Stalled::number_of_iteration (refreshes of the axis with no target)
    std::atomic<uint32_t> speed_changes; ///< SuddenSpeedChange events (more than 20 mm/s)
    std::atomic<uint32_t> big_speed_changes; ///< SuddenSpeedChange events of more than big_speed_change mm/s

    static constexpr float big_speed_change = 100.0f;

    static void add(std::atomic<uint32_t> &counter, uint32_t n = 1) {
        counter.store(counter.load(std::memory_order_relaxed) + n, std::memory_order_relaxed);
    }
};

/// Index 0 is X, index 1 is Y (the axis_index of the axis state).
extern std::array<ProblemCounts, 2> problem_counts;
} // namespace phase_stepping
