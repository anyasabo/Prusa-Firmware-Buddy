/// @file
#include "skew.hpp"

#if ENABLED(SKEW_CORRECTION)

    #include <inc/MarlinConfig.h>
    #include <module/motion.h>
    #include <module/planner.h>
    #include <config_store/store_instance.hpp>

namespace {

/// Calls \p change (which changes the skew) and keeps the machine position, as set_bed_leveling_enabled() does
template <typename F>
void keep_machine_position(const F &change) {
    planner.synchronize();
    const auto machine_position = current_machine_position();
    change();
    set_current_position(to_native_pos(machine_position));
    sync_plan_position();
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

    if (factor != planner.skew_factor) {
        keep_machine_position([&] { planner.skew_factor = factor; });
    }
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
