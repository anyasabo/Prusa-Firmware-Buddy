#pragma once
// Stand-in for the planner: a plain block queue that the test fills. The fields of block_t are the
// ones that PreciseStepping reads.
#include "../inc/MarlinConfig.h"
#include "../core/mtypes.hpp"

#include <deque>

constexpr const int PLANNER_STEPS_MULTIPLIER = 4;

struct block_flags_t {
    bool sync_position = false;
};

struct block_t {
    block_flags_t flag;
    bool busy = false;
    bool is_sync() const { return flag.sync_position; }
    bool is_move() const { return !is_sync(); }

    uint8_t direction_bits = 0;
    float nominal_speed = 0, millimeters = 0, acceleration = 0;
    xyze_msteps_t msteps {};
    xyze_steps_t sync_step_position {};
    float initial_speed = 0, final_speed = 0;
};

class Planner {
public:
    struct Settings {
        float axis_steps_per_mm[XYZE_N];
    };
    static inline Settings settings {};
    static inline float mm_per_step[XYZE_N] {};
    static inline float mm_per_half_step[XYZE_N] {};
    static inline float mm_per_mstep[XYZE_N] {};
    static inline uint32_t delay_before_delivering = 0;

    // Blocks that PreciseStepping did not take yet, then blocks that it took but the step ISR did not
    // discard yet.
    static inline std::deque<block_t> unprocessed;
    static inline std::deque<block_t> processed;

    static void set_steps_per_mm(const float (&steps_per_mm)[XYZE_N]) {
        for (int i = 0; i < XYZE_N; ++i) {
            settings.axis_steps_per_mm[i] = steps_per_mm[i];
            mm_per_step[i] = 1.f / steps_per_mm[i];
            mm_per_half_step[i] = 0.5f * mm_per_step[i];
            mm_per_mstep[i] = mm_per_step[i] / PLANNER_STEPS_MULTIPLIER;
        }
    }

    static block_t *get_current_unprocessed_block() { return unprocessed.empty() ? nullptr : &unprocessed.front(); }
    static void discard_current_unprocessed_block() {
        processed.push_back(unprocessed.front());
        unprocessed.pop_front();
    }
    static block_t *get_current_processed_block() { return processed.empty() ? nullptr : &processed.front(); }
    static void discard_current_block() { processed.pop_front(); }

    static bool is_full() { return false; }
    static bool is_recalculating() { return false; }
    static uint8_t nonbusy_movesplanned() { return clamp8(unprocessed.size()); }
    static uint8_t optimized_movesplanned() { return clamp8(unprocessed.size()); }
    static uint8_t movesplanned_processed() { return clamp8(processed.size()); }
    static uint8_t movesplanned() { return clamp8(unprocessed.size() + processed.size()); }
    static void clear_block_buffer() {
        unprocessed.clear();
        processed.clear();
    }
    static void synchronize() {}
    static bool draining() { return false; }

private:
    static uint8_t clamp8(size_t n) { return n > 255 ? 255 : uint8_t(n); }
};

inline Planner planner;

// As the real planner.h
#include "../feature/precise_stepping/precise_stepping.hpp"
