#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <power_panic_plan.hpp>

#include <cstring>
#include <string_view>

using namespace power_panic::plan;

TEST_CASE("power_panic_plan: can_auto_recover") {
    constexpr float max_diff = 10;

    SECTION("nothing extruded since the print start: resume a cold bed too") {
        CHECK(can_auto_recover(100, 100, 100, 20, max_diff));
    }
    SECTION("no bed target") {
        CHECK(can_auto_recover(0, 100, 0, 20, max_diff));
    }
    SECTION("bed at or above the target") {
        CHECK(can_auto_recover(0, 100, 60, 60, max_diff));
        CHECK(can_auto_recover(0, 100, 60, 70, max_diff));
    }
    SECTION("bed colder than the target") {
        CHECK(can_auto_recover(0, 100, 60, 50.5f, max_diff));
        CHECK_FALSE(can_auto_recover(0, 100, 60, 50, max_diff));
        CHECK_FALSE(can_auto_recover(0, 100, 60, 20, max_diff));
    }
}

TEST_CASE("power_panic_plan: needs_skew_conversion") {
    CHECK(needs_skew_conversion(true, false, false));
    CHECK_FALSE(needs_skew_conversion(false, false, false));
    CHECK_FALSE(needs_skew_conversion(true, true, false));
    CHECK_FALSE(needs_skew_conversion(true, false, true));
}

TEST_CASE("power_panic_plan: finish_kind") {
    const bool was_paused = GENERATE(false, true);
    const FinishKind plain = was_paused ? FinishKind::pause : FinishKind::recovery;

    SECTION("INDX toolchange has priority") {
        CHECK(finish_kind({ .indx_toolchange_in_progress = true, .crash_in_toolchange_area = true, .toolchanger_enabled = true, .was_paused = was_paused }) == FinishKind::indx_toolchange);
    }
    SECTION("tool crash recovery needs an enabled toolchanger") {
        CHECK(finish_kind({ .indx_toolchange_in_progress = false, .crash_in_toolchange_area = true, .toolchanger_enabled = true, .was_paused = was_paused }) == FinishKind::toolcrash);
        CHECK(finish_kind({ .indx_toolchange_in_progress = false, .crash_in_toolchange_area = true, .toolchanger_enabled = false, .was_paused = was_paused }) == plain);
    }
    SECTION("outside the toolchange area") {
        CHECK(finish_kind({ .indx_toolchange_in_progress = false, .crash_in_toolchange_area = false, .toolchanger_enabled = true, .was_paused = was_paused }) == plain);
    }
}

namespace {
ResumeInput printing_input() {
    return {
        .crash_in_toolchange_area = false,
        .indx_toolchange_in_progress = false,
        .recover_axis_state = true,
        .x_or_y_homed = true,
        .was_paused = false,
        .current_z = 10.4f,
        .crash_z = 10.2f,
        .z_homing_height = 5,
    };
}
} // namespace

TEST_CASE("power_panic_plan: resume_steps") {
    ResumeInput in = printing_input();

    SECTION("printing: rehome, then heat") {
        const ResumeSteps steps = resume_steps(in);
        CHECK(steps.next == ResumeNext::wait_for_heaters);
        REQUIRE(steps.rehome_z_lift.has_value());
        CHECK(*steps.rehome_z_lift == 5 - (10.4f - 10.2f));
    }
    SECTION("paused: rehome, then park, no heat") {
        in.was_paused = true;
        const ResumeSteps steps = resume_steps(in);
        CHECK(steps.next == ResumeNext::park_for_pause);
        CHECK(steps.rehome_z_lift.has_value());
    }
    SECTION("no rehome if the G-code does not recover the axis state") {
        in.recover_axis_state = false;
        CHECK_FALSE(resume_steps(in).rehome_z_lift.has_value());
    }
    SECTION("no rehome if X and Y were not homed") {
        in.x_or_y_homed = false;
        const ResumeSteps steps = resume_steps(in);
        CHECK_FALSE(steps.rehome_z_lift.has_value());
        CHECK(steps.next == ResumeNext::wait_for_heaters);
    }
    SECTION("INDX toolchange: no rehome, no heat") {
        in.indx_toolchange_in_progress = true;
        in.was_paused = GENERATE(false, true);
        const ResumeSteps steps = resume_steps(in);
        CHECK(steps.next == ResumeNext::finish);
        CHECK_FALSE(steps.rehome_z_lift.has_value());
    }
}

TEST_CASE("power_panic_plan: rehome_z_lift") {
    CHECK(rehome_z_lift(10, 10, 5) == 5);
    CHECK(rehome_z_lift(12, 10, 5) == 3);
    CHECK(rehome_z_lift(15, 10, 5) == 0);
    CHECK(rehome_z_lift(30, 10, 5) == 0);
    // The power panic lifted less than the crash Z (for example a paused print, where the crash
    // position is the print position and Z is the park Z): lift more.
    CHECK(rehome_z_lift(8, 10, 5) == 7);
}

TEST_CASE("power_panic_plan: rehome_gcode") {
    CHECK(std::string_view(rehome_gcode(0).data()) == "G28 X Y D R0.000000");
    CHECK(std::string_view(rehome_gcode(4.8f).data()) == "G28 X Y D R4.800000");
    // The largest Z of all printers is below 1000 mm, so all realistic lifts fit
    CHECK(std::string_view(rehome_gcode(999.5f).data()) == "G28 X Y D R999.500000");
}

TEST_CASE("power_panic_plan: unpark_target") {
    const Xyz saved { 1, 2, 3 };
    const Xyz current { 10, 20, 30 };
    CHECK(unpark_target(saved, current, true, true) == saved);
    CHECK(unpark_target(saved, current, false, true) == Xyz { 10, 20, 3 });
    CHECK(unpark_target(saved, current, true, false) == Xyz { 1, 2, 30 });
    CHECK(unpark_target(saved, current, false, false) == current);
}

TEST_CASE("power_panic_plan: INDX tools") {
    constexpr uint8_t before = 1;
    constexpr uint8_t target = 3;
    CHECK(resume_tool(before, target, false) == before);
    CHECK(resume_tool(before, target, true) == target);
    CHECK(held_tool(before, target, false) == before);
    CHECK(held_tool(before, target, true) == target);
}

TEST_CASE("power_panic_plan: chamber target round trip") {
    CHECK(save_chamber_target(std::nullopt) == chamber_temp_off);
    CHECK(restore_chamber_target(chamber_temp_off) == std::nullopt);
    CHECK(restore_chamber_target(save_chamber_target(45.f)) == 45.f);
    CHECK(restore_chamber_target(save_chamber_target(0.f)) == 0.f);
}

TEST_CASE("power_panic_plan: input shaper axis round trip") {
    using input_shaper::AxisConfig;
    CHECK(restore_axis_config(save_axis_config(std::nullopt)) == std::nullopt);

    const AxisConfig config = input_shaper::axis_y_default;
    const auto restored = restore_axis_config(save_axis_config(config));
    REQUIRE(restored.has_value());
    CHECK(*restored == config);
}

TEST_CASE("power_panic_plan: escape_move") {
    EscapeInput in {
        .x = 0,
        .y = 0,
        .rect_min_x = 0,
        .rect_min_y = 0,
        .rect_max_x = 200,
        .rect_max_y = 200,
        .x_min_print_pos = -5,
        .x_max_print_pos = 205,
        .y_min_print_pos = -3,
        .y_max_print_pos = 203,
        .x_axis_length = 250,
        .corexy = false,
        .xy_known = true,
    };

    SECTION("bedslinger: X edge nearest to the head") {
        in.x = 50;
        in.y = 100;
        CHECK(escape_move(in) == EscapeMove { EscapeAxis::x, -5 });
        in.x = 150;
        CHECK(escape_move(in) == EscapeMove { EscapeAxis::x, 205 });
        // Y is nearer to an edge, but the head cannot escape along Y
        in.y = 195;
        CHECK(escape_move(in) == EscapeMove { EscapeAxis::x, 205 });
    }
    SECTION("CoreXY: the nearest edge of X or Y") {
        in.corexy = true;
        in.x = 100;
        in.y = 190;
        CHECK(escape_move(in) == EscapeMove { EscapeAxis::y, 203 });
        in.y = 10;
        CHECK(escape_move(in) == EscapeMove { EscapeAxis::y, -3 });
        in.x = 5;
        CHECK(escape_move(in) == EscapeMove { EscapeAxis::x, -5 });
    }
    SECTION("unknown position: towards the X endstop") {
        in.xy_known = false;
        in.corexy = GENERATE(false, true);
        in.x = 100;
        in.y = 190;
        CHECK(escape_move(in) == EscapeMove { EscapeAxis::x, -150 });
    }
}
