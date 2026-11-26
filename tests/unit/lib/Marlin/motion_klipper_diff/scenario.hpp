#pragma once
// Builds motion scenarios (configuration + planner blocks) from bytes, for the randomized tests and
// the fuzz target. Every scenario is feasible as the planner would make it: the start and end speeds
// can be reached within the block, and each block starts at the end speed of the one before.
#include "motion_model.hpp"

#include <cstddef>
#include <cstdint>

namespace motion_diff {

/// Reads bounded values from bytes; returns 0 when the bytes are used up (as FuzzedDataProvider).
class ByteReader {
public:
    ByteReader(const uint8_t *data, size_t size)
        : data_(data)
        , size_(size) {}

    uint32_t u32() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v = (v << 8) | byte();
        }
        return v;
    }
    uint8_t byte() { return pos_ < size_ ? data_[pos_++] : 0; }
    /// Uniform in [lo, hi].
    float range(float lo, float hi) { return lo + (hi - lo) * float(u32() >> 8) / float(0xFFFFFF); }
    bool empty() const { return pos_ >= size_; }

private:
    const uint8_t *data_;
    size_t size_;
    size_t pos_ = 0;
};

struct Scenario {
    Config config;
    std::vector<Block> blocks;
};

/// Limits of a scenario (the CORE One L motion limits, a little wider).
struct ScenarioLimits {
    int max_blocks = 12;
    float max_xy_mm = 40.f; // per block
    float max_z_mm = 2.f;
    float max_speed = 400.f;
    float max_accel = 12000.f;
    bool allow_shaper = true;
    bool allow_pa = true;
};

Scenario make_scenario(ByteReader &in, const ScenarioLimits &limits = {});

} // namespace motion_diff
