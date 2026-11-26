/// @file
#include "skew.hpp"

#if ENABLED(SKEW_CORRECTION)

    #include <inc/MarlinConfig.h>
    #include <module/motion.h>
    #include <module/planner.h>
    #include <config_store/store_instance.hpp>

namespace {

/// Calls \p change (which changes the skew) and keeps the machine position: the nozzle does not move, and
/// current_position (XYZ) changes.
///
/// Unlike set_bed_leveling_enabled(), this takes the planner position and does not sync the planner from
/// current_position. In the edge band of skew() and unskew() (COORDINATE_OKAY), to_machine_pos(to_native_pos(m))
/// is not m, so a sync_plan_position() would move the planner position away from the real stepper position. One
/// example is the CORE One L nozzle cleaner touchpoint (machine Y -7.5). skew_tests.cpp tests this.
template <typename F>
void keep_machine_position(const F &change) {
    planner.synchronize();
    const MachinePosXYZ machine_position = planner.get_machine_position_mm().xyz();
    change();
    current_position.set(to_native_pos(machine_position));
}

constexpr skew_factor_t no_skew { 0, 0, 0 };

} // namespace

skew_factor_t stored_skew_factor() {
    const skew_factor_t f = config_store().skew_factor.get();
    const auto valid = [](float v) { return WITHIN(v, SKEW_FACTOR_MIN, SKEW_FACTOR_MAX); };
    if (valid(f.xy) && valid(f.xz) && valid(f.yz)) {
        return f;
    }
    return { XY_SKEW_FACTOR, XZ_SKEW_FACTOR, YZ_SKEW_FACTOR };
}

void load_skew_factor() {
    static bool booted = false;
    const skew_factor_t factor = stored_skew_factor();

    if (!booted) {
        // setup() syncs the planner position after this
        booted = true;
        planner.skew_factor = factor;
        return;
    }

    set_skew_factor(factor);
}

void set_skew_factor(const skew_factor_t &factor) {
    if (factor == planner.skew_factor) {
        return;
    }

    planner.synchronize();
    if (planner.draining()) {
        // A crash or an abort emptied the queue during the wait. Crash recovery replays the moves before this
        // M852 with the old factors, and then this M852.
        return;
    }

    keep_machine_position([&] { planner.skew_factor = factor; });
}

xyz_pos_t native_pos_with_skew(const xyz_pos_t &native_without_skew) {
    // With the skew off, the machine XY is the native XY, and MBL uses the same XY. Thus unskew() of the native
    // position gives a native position that skew() maps back to that XY, at the same Z.
    xyz_pos_t result = native_without_skew;
    planner.unskew(result);
    return result;
}

void disable_skew() {
    if (planner.skew_disabled == 0 && planner.skew_factor != no_skew) {
        keep_machine_position([] { ++planner.skew_disabled; });
    } else {
        ++planner.skew_disabled;
    }
}

void restore_skew() {
    if (planner.skew_disabled == 0) {
        // Not balanced with disable_skew(), do not underflow
        return;
    }

    if (planner.skew_disabled == 1 && planner.skew_factor != no_skew) {
        keep_machine_position([] { --planner.skew_disabled; });
    } else {
        --planner.skew_disabled;
    }
}

#endif
