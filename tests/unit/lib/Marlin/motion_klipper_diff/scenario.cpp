#include "scenario.hpp"

#include <algorithm>
#include <cmath>

namespace motion_diff {

namespace {
    input_shaper::AxisConfig make_shaper(ByteReader &in) {
        static constexpr input_shaper::Type types[] = {
            input_shaper::Type::zv,
            input_shaper::Type::zvd,
            input_shaper::Type::mzv,
            input_shaper::Type::ei,
            input_shaper::Type::ei_2hump,
            input_shaper::Type::ei_3hump,
        };
        return {
            .type = types[in.byte() % std::size(types)],
            // input_shaper::frequency_safe_min/max (set_axis_config() clamps to these)
            .frequency = in.range(input_shaper::frequency_safe_min, input_shaper::frequency_safe_max),
            .damping_ratio = in.range(0.f, 0.3f),
            .vibration_reduction = in.range(10.f, 30.f),
        };
    }
} // namespace

Scenario make_scenario(ByteReader &in, const ScenarioLimits &limits) {
    Scenario s;
    Config &c = s.config;

    const uint8_t flags = in.byte();
    if (flags & 1) {
        c.steps_per_mm[0] = c.steps_per_mm[1] = 101.587f; // 1.5GT belts (AXIS_STEPS_PER_UNIT_15GT_XY)
    }
    if (flags & 2) {
        c.steps_per_mm[3] = 550.f; // INDX extruder
    }
    if (limits.allow_shaper) {
        if (flags & 4) {
            c.shaper[0] = make_shaper(in);
        }
        if (flags & 8) {
            c.shaper[1] = make_shaper(in);
        }
        if ((flags & 16) && (flags & 32)) {
            c.shaper[2] = make_shaper(in);
        }
    }
    if (limits.allow_pa && (flags & 64)) {
        c.pressure_advance = in.range(0.005f, 0.2f);
        // 0.01 s: the minimum of the fix for M572 W0 (branch anya/pa-smooth-time-min, klipper-audit K2)
        c.smooth_time = in.range(0.01f, 0.2f);
    }
    // Start anywhere on a CORE One L sized machine (A + B even: a whole number of X/Y mini-steps
    // per step pair, as after homing).
    const int32_t a = int32_t(in.range(0.f, 60000.f));
    const int32_t b = int32_t(in.range(-30000.f, 30000.f));
    c.start_steps = { a, b - ((a + b) & 1), int32_t(in.range(0.f, 2.4e5f)) };

    const int n = 1 + in.byte() % limits.max_blocks;
    for (int i = 0; i < n && (i == 0 || !in.empty()); ++i) {
        Block blk;
        const uint8_t kind = in.byte();
        const bool xy = kind % 5 != 0; // else E-only (retraction, deretraction) or Z
        if (xy) {
            blk.dx = in.range(-limits.max_xy_mm, limits.max_xy_mm);
            blk.dy = (kind & 8) ? 0. : in.range(-limits.max_xy_mm, limits.max_xy_mm);
            if (kind & 16) {
                blk.de = std::hypot(blk.dx, blk.dy) * in.range(0.f, 0.08f); // extrusion
            }
        } else if (kind & 1) {
            blk.dz = in.range(-limits.max_z_mm, limits.max_z_mm);
        } else {
            blk.de = in.range(-5.f, 5.f);
        }
        const double length = (blk.dx || blk.dy || blk.dz) ? std::sqrt(blk.dx * blk.dx + blk.dy * blk.dy + blk.dz * blk.dz) : std::abs(blk.de);
        if (length < 1e-3) {
            continue;
        }
        blk.accel = in.range(50.f, limits.max_accel);
        blk.v_cruise = in.range(1.f, limits.max_speed);
        if (!xy) {
            // CORE One L Z and E limits are much lower than X/Y (DEFAULT_MAX_FEEDRATE, M201)
            blk.v_cruise = std::min(blk.v_cruise, (kind & 1) ? 30.f : 120.f);
            blk.accel = std::min(blk.accel, (kind & 1) ? 1000.f : 5000.f);
        }
        blk.v_start = blk.v_cruise;
        blk.v_end = in.range(0.f, blk.v_cruise);
        blk.halt_before = !s.blocks.empty() && (kind & 0xE0) == 0xE0; // 1 in 8: the planner queue runs dry
        s.blocks.push_back(blk);
    }

    // As the planner: start and end at rest, each block starts at the end speed of the block before,
    // and both ends of a block can be reached within it.
    auto reach2 = [](const Block &b) {
        const double length = (b.dx || b.dy || b.dz) ? std::sqrt(b.dx * b.dx + b.dy * b.dy + b.dz * b.dz) : std::abs(b.de);
        return float(2. * b.accel * length);
    };
    if (!s.blocks.empty()) {
        s.blocks.back().v_end = 0.f;
    }
    for (size_t i = s.blocks.size(); i-- > 0;) { // backward pass
        Block &b = s.blocks[i];
        b.v_start = std::min(b.v_start, std::sqrt(b.v_end * b.v_end + reach2(b)));
        if (b.halt_before) {
            b.v_start = 0.f;
        }
        if (i > 0) {
            s.blocks[i - 1].v_end = std::min(s.blocks[i - 1].v_end, b.v_start);
        }
    }
    float v_prev = 0.f;
    for (Block &b : s.blocks) { // forward pass
        b.v_start = b.halt_before ? 0.f : v_prev;
        b.v_end = std::min(b.v_end, std::sqrt(b.v_start * b.v_start + reach2(b)));
        v_prev = b.v_end;
    }
    return s;
}

} // namespace motion_diff
