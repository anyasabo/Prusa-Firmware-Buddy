#include "g28_plan.hpp"

#include <algorithm>

namespace g28 {

Plan::Plan(const PlanFeatures &features, const PlanInput &input)
    : requested(input.requested) {
    for (uint8_t i = 0; i < requirements.size(); i++) {
        requirements[i].initial_level = input.initial_levels[i];
        if (input.requested[i]) {
            expand(i, input.precise ? AxisHomeLevel::full : AxisHomeLevel::imprecise, !input.only_if_needed);
        }
    }

    // On Z_SAFE_HOMING, if we need to home Z, we need to have X and Y homed as well
    if (features.z_safe_homing && home_at_all(axis_z)) {
        expand(axis_x, AxisHomeLevel::imprecise, false);
        expand(axis_y, AxisHomeLevel::imprecise, false);
    }

    // For Z homing, we need a tool pick. That means that we need precise homing for the toolchange.
    // !! This one is a MUST - without this, the toolchange would trigger a nested G28 internally, which would break things
    if (input.z_needs_tool_pick && home_at_all(axis_z)) {
        expand(axis_x, AxisHomeLevel::full, false);
        expand(axis_y, AxisHomeLevel::full, false);
    }

    if (features.codependent_xy_homing) {
        join_xy();
    }

    // Precise COREXY homing is XY codependent, even if the imprecise homing isn't
    if (features.precise_homing_corexy && (home_to_level(axis_x, AxisHomeLevel::full) || home_to_level(axis_y, AxisHomeLevel::full))) {
        join_xy();
    }
}

void Plan::expand(uint8_t axis, AxisHomeLevel required_level, bool force) {
    auto &r = requirements[axis];
    r.required_level = std::max(r.required_level, required_level);
    r.force |= force;
}

void Plan::join_xy() {
    auto &x = requirements[axis_x];
    auto &y = requirements[axis_y];
    expand(axis_x, y.required_level, y.force);
    // The pair is homed only as well as its worse axis. A copy of the X level would hide an unhomed Y.
    x.initial_level = std::min(x.initial_level, y.initial_level);
    y = x;
}

bool Plan::home_at_all(uint8_t axis) const {
    const auto &r = requirements[axis];
    return r.force || (r.initial_level < r.required_level);
}

bool Plan::home_to_level(uint8_t axis, AxisHomeLevel level) const {
    const auto &r = requirements[axis];
    return (level <= r.required_level) && (r.force || r.initial_level < level);
}

bool Plan::nothing_to_do() const {
    return !home_at_all(axis_x) && !home_at_all(axis_y) && !home_at_all(axis_z);
}

bool Plan::refines_xy() const {
    return home_to_level(axis_x, AxisHomeLevel::full) || home_to_level(axis_y, AxisHomeLevel::full);
}

bool Plan::measures_loadcell_liveness() const {
    return home_at_all(axis_z) && (home_to_level(axis_x, AxisHomeLevel::imprecise) || home_to_level(axis_y, AxisHomeLevel::imprecise));
}

bool Plan::resets_max_printed_z() const {
    return requested[axis_z];
}

} // namespace g28
