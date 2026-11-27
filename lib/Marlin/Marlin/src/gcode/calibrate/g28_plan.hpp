#pragma once

#include "../../module/axis_home_level.hpp"

#include <array>
#include <cstdint>

/// Decisions of G28 about which axes to home and to which level.
/// The plan has no side effects: G28 gives it the request and the current home levels, and it tells G28 what to do.
namespace g28 {

/// Axis indexes of the plan, the same as X_AXIS, Y_AXIS and Z_AXIS
inline constexpr uint8_t axis_x = 0;
inline constexpr uint8_t axis_y = 1;
inline constexpr uint8_t axis_z = 2;

/// Configuration options of the printer that change the plan
struct PlanFeatures {
    /// Z_SAFE_HOMING: Z homes at a point on the bed, so X and Y must be homed first
    bool z_safe_homing = false;

    /// CODEPENDENT_XY_HOMING: X and Y home together
    bool codependent_xy_homing = false;

    /// HAS_PRECISE_HOMING_COREXY(): the precise refinement measures X and Y together
    bool precise_homing_corexy = false;
};

struct PlanInput {
    /// Axes that the G28 names (X, Y, Z)
    std::array<bool, 3> requested = { false, false, false };

    /// Home to AxisHomeLevel::full, not only to AxisHomeLevel::imprecise
    bool precise = true;

    /// Home only the axes that are not homed to the required level
    bool only_if_needed = false;

    /// Home levels of X, Y and Z when the G28 starts
    std::array<AxisHomeLevel, 3> initial_levels = { AxisHomeLevel::not_homed, AxisHomeLevel::not_homed, AxisHomeLevel::not_homed };

    /// Z homing must first pick a tool (a toolchanger with no tool selected)
    bool z_needs_tool_pick = false;
};

class Plan {
public:
    Plan(const PlanFeatures &features, const PlanInput &input);

    /// \returns whether the G28 homes the axis at all
    bool home_at_all(uint8_t axis) const;

    /// \returns whether the G28 homes the axis to (or above) the level.
    /// A G28 can require only the precise refinement and not the base homing:
    /// then home_to_level(axis, imprecise) is false and home_to_level(axis, full) is true.
    bool home_to_level(uint8_t axis, AxisHomeLevel level = AxisHomeLevel::full) const;

    /// \returns whether no axis needs homing, so the G28 succeeds without a move
    bool nothing_to_do() const;

    /// \returns whether the G28 does the precise CoreXY refinement of X and Y
    bool refines_xy() const;

    /// \returns whether the G28 measures the load cell during the XY homing.
    /// The measurement proves that the load cell is alive before the Z homing trusts it.
    bool measures_loadcell_liveness() const;

    /// \returns whether the G28 can forget the highest printed Z.
    /// A Z homing proves that nothing is on the bed.
    bool resets_max_printed_z() const;

private:
    struct Requirement {
        AxisHomeLevel required_level = AxisHomeLevel::not_homed;

        /// The axis homes even if it is already homed to the required level
        bool force = false;

        AxisHomeLevel initial_level = AxisHomeLevel::not_homed;
    };

    void expand(uint8_t axis, AxisHomeLevel required_level, bool force);
    void join_xy();

    std::array<Requirement, 3> requirements;
};

} // namespace g28
