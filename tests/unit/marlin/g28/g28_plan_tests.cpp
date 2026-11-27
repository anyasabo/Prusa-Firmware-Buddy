/// @file
/// Host tests of the G28 homing plan (lib/Marlin/Marlin/src/gcode/calibrate/g28_plan.cpp)
#include <gcode/calibrate/g28_plan.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace g28;

namespace {

constexpr auto not_homed = AxisHomeLevel::not_homed;
constexpr auto imprecise = AxisHomeLevel::imprecise;
constexpr auto full = AxisHomeLevel::full;

/// CORE One and CORE One L
constexpr PlanFeatures coreone {
    .z_safe_homing = true,
    .codependent_xy_homing = true,
    .precise_homing_corexy = true,
};

/// XL: precise CoreXY homing, but X and Y can home separately
constexpr PlanFeatures xl {
    .z_safe_homing = true,
    .codependent_xy_homing = false,
    .precise_homing_corexy = true,
};

/// MK4: cartesian
constexpr PlanFeatures mk4 {
    .z_safe_homing = true,
    .codependent_xy_homing = false,
    .precise_homing_corexy = false,
};

PlanInput g28_all(std::array<AxisHomeLevel, 3> levels) {
    return { .requested = { true, true, true }, .initial_levels = levels };
}

PlanInput g28_o_all(std::array<AxisHomeLevel, 3> levels) {
    return { .requested = { true, true, true }, .only_if_needed = true, .initial_levels = levels };
}

} // namespace

TEST_CASE("g28_plan: plain G28 homes everything to full level") {
    const auto features = GENERATE(coreone, xl, mk4);
    const auto level = GENERATE(not_homed, imprecise, full);
    const Plan plan(features, g28_all({ level, level, level }));

    CHECK_FALSE(plan.nothing_to_do());
    for (uint8_t axis : { axis_x, axis_y, axis_z }) {
        CHECK(plan.home_at_all(axis));
        CHECK(plan.home_to_level(axis, imprecise));
        CHECK(plan.home_to_level(axis, full));
    }
    CHECK(plan.refines_xy());
    CHECK(plan.measures_loadcell_liveness());
    CHECK(plan.resets_max_printed_z());
}

TEST_CASE("g28_plan: G28 O does nothing when all axes are homed precisely") {
    const auto features = GENERATE(coreone, xl, mk4);
    const Plan plan(features, g28_o_all({ full, full, full }));

    CHECK(plan.nothing_to_do());
    CHECK_FALSE(plan.refines_xy());
    CHECK_FALSE(plan.measures_loadcell_liveness());
}

TEST_CASE("g28_plan: G28 O with imprecise XY only refines") {
    const Plan plan(coreone, g28_o_all({ imprecise, imprecise, full }));

    CHECK_FALSE(plan.nothing_to_do());
    CHECK(plan.home_at_all(axis_x));
    CHECK(plan.home_at_all(axis_y));
    CHECK_FALSE(plan.home_to_level(axis_x, imprecise));
    CHECK_FALSE(plan.home_to_level(axis_y, imprecise));
    CHECK(plan.home_to_level(axis_x, full));
    CHECK(plan.home_to_level(axis_y, full));
    CHECK(plan.refines_xy());
    CHECK_FALSE(plan.home_at_all(axis_z));
    CHECK_FALSE(plan.measures_loadcell_liveness());
}

TEST_CASE("g28_plan: G28 O with imprecise XY and unhomed Z refines without the liveness window") {
    // The XY base homing does not run, so the load cell has nothing to feel
    const Plan plan(coreone, g28_o_all({ imprecise, imprecise, not_homed }));

    CHECK(plan.home_at_all(axis_z));
    CHECK(plan.refines_xy());
    CHECK_FALSE(plan.home_to_level(axis_x, imprecise));
    CHECK_FALSE(plan.measures_loadcell_liveness());
}

TEST_CASE("g28_plan: G28 O with unhomed Z and XY measures the load cell") {
    const Plan plan(coreone, g28_o_all({ not_homed, not_homed, not_homed }));

    CHECK(plan.home_to_level(axis_x, imprecise));
    CHECK(plan.measures_loadcell_liveness());
}

TEST_CASE("g28_plan: XY homing without Z homing does not measure the load cell") {
    const Plan plan(coreone, { .requested = { true, true, false }, .initial_levels = { full, full, full } });

    CHECK(plan.home_to_level(axis_x, imprecise));
    CHECK_FALSE(plan.home_at_all(axis_z));
    CHECK_FALSE(plan.measures_loadcell_liveness());
}

TEST_CASE("g28_plan: G28 I does not refine") {
    const auto features = GENERATE(coreone, xl, mk4);
    const Plan plan(features, { .requested = { true, true, true }, .precise = false, .initial_levels = { full, full, full } });

    CHECK(plan.home_to_level(axis_x, imprecise));
    CHECK_FALSE(plan.home_to_level(axis_x, full));
    CHECK_FALSE(plan.refines_xy());
}

TEST_CASE("g28_plan: G28 O I accepts imprecise XY") {
    const Plan plan(coreone, { .requested = { true, true, true }, .precise = false, .only_if_needed = true, .initial_levels = { imprecise, imprecise, full } });

    CHECK(plan.nothing_to_do());
}

TEST_CASE("g28_plan: G28 Z on Z_SAFE_HOMING homes XY only if they are not homed") {
    SECTION("XY not homed") {
        const Plan plan(mk4, { .requested = { false, false, true }, .initial_levels = { not_homed, not_homed, full } });
        CHECK(plan.home_at_all(axis_z));
        CHECK(plan.home_to_level(axis_x, imprecise));
        CHECK(plan.home_to_level(axis_y, imprecise));
        CHECK_FALSE(plan.refines_xy());
    }
    SECTION("XY homed imprecisely") {
        const Plan plan(mk4, { .requested = { false, false, true }, .initial_levels = { imprecise, imprecise, full } });
        CHECK(plan.home_at_all(axis_z));
        CHECK_FALSE(plan.home_at_all(axis_x));
        CHECK_FALSE(plan.home_at_all(axis_y));
    }
    SECTION("X homed, Y not") {
        const Plan plan(mk4, { .requested = { false, false, true }, .initial_levels = { imprecise, not_homed, full } });
        CHECK_FALSE(plan.home_at_all(axis_x));
        CHECK(plan.home_to_level(axis_y, imprecise));
    }
}

TEST_CASE("g28_plan: Z homing on a toolchanger without a tool requires precise XY") {
    const Plan plan(xl, { .requested = { false, false, true }, .initial_levels = { imprecise, imprecise, full }, .z_needs_tool_pick = true });

    CHECK(plan.home_at_all(axis_z));
    CHECK_FALSE(plan.home_to_level(axis_x, imprecise));
    CHECK(plan.home_to_level(axis_x, full));
    CHECK(plan.refines_xy());
}

TEST_CASE("g28_plan: the tool pick requirement applies only to Z homing") {
    const Plan plan(xl, { .requested = { false, false, true }, .only_if_needed = true, .initial_levels = { imprecise, imprecise, full }, .z_needs_tool_pick = true });

    CHECK(plan.nothing_to_do());
}

TEST_CASE("g28_plan: CODEPENDENT_XY_HOMING homes Y with X") {
    const Plan plan(coreone, { .requested = { true, false, false }, .precise = false, .initial_levels = { full, full, full } });

    CHECK(plan.home_to_level(axis_x, imprecise));
    CHECK(plan.home_to_level(axis_y, imprecise));
    CHECK_FALSE(plan.home_at_all(axis_z));
}

TEST_CASE("g28_plan: without CODEPENDENT_XY_HOMING, imprecise X homing leaves Y") {
    const Plan plan(xl, { .requested = { true, false, false }, .precise = false, .initial_levels = { full, full, full } });

    CHECK(plan.home_to_level(axis_x, imprecise));
    CHECK_FALSE(plan.home_at_all(axis_y));
}

TEST_CASE("g28_plan: precise CoreXY refinement joins X and Y") {
    const Plan plan(xl, { .requested = { true, false, false }, .initial_levels = { full, full, full } });

    CHECK(plan.home_to_level(axis_x, imprecise));
    CHECK(plan.home_to_level(axis_y, imprecise));
    CHECK(plan.refines_xy());
}

TEST_CASE("g28_plan: cartesian precise X homing leaves Y") {
    const Plan plan(mk4, { .requested = { true, false, false }, .initial_levels = { full, full, full } });

    CHECK(plan.home_to_level(axis_x, full));
    CHECK_FALSE(plan.home_at_all(axis_y));
}

TEST_CASE("g28_plan: G28 O homes Z when only Z is not homed") {
    const Plan plan(coreone, g28_o_all({ full, full, not_homed }));

    CHECK(plan.home_at_all(axis_z));
    CHECK_FALSE(plan.home_at_all(axis_x));
    CHECK_FALSE(plan.home_at_all(axis_y));
    CHECK_FALSE(plan.measures_loadcell_liveness());
    CHECK(plan.resets_max_printed_z());
}
