#include <module/motion.h>
#include <module/planner.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

constexpr float epsilon = std::numeric_limits<float>::epsilon();
constexpr float tolerance = 0.000001f;
const float roundtrip_tolerance = std::sqrt(tolerance); // std::sqrt is constexpr only in GCC

TEST_CASE("machine_pos_conv") {
    SECTION("Fade off") {
        Planner::set_z_fade_height(0);

        UBL::z_correction = 3;
        CHECK(to_machine_pos(xyz_pos_t { 0, 0, 1 }) == MachinePosXYZ { 0, 0, 4 });
        CHECK(to_native_pos(MachinePosXYZ { 0, 0, 4 }) == xyz_pos_t { 0, 0, 1 });

        UBL::z_correction = -2;
        CHECK(to_machine_pos(xyz_pos_t { 0, 0, 5 }) == MachinePosXYZ { 0, 0, 3 });
        CHECK(to_native_pos(MachinePosXYZ { 0, 0, 3 }) == xyz_pos_t { 0, 0, 5 });
    }

    SECTION("Fade on") {
        constexpr float fade_height = 10.f;
        Planner::set_z_fade_height(fade_height);

        const float z_correction = GENERATE(0.0f, 0.013f, -0.013f, 0.47f, -0.47f, 1.73f, -1.73f);
        UBL::z_correction = z_correction;

        CAPTURE(z_correction);

        SECTION("Around") {
            {
                constexpr xyz_pos_t vec { 0, 0, fade_height - epsilon };
                CHECK_THAT(to_native_pos(to_machine_pos(vec)).z, Catch::Matchers::WithinAbs(vec.z, tolerance));
            }

            CHECK(to_machine_pos(xyz_pos_t { 0, 0, fade_height }) == MachinePosXYZ { 0, 0, fade_height });
            CHECK(to_native_pos(MachinePosXYZ { 0, 0, fade_height }) == xyz_pos_t { 0, 0, fade_height });

            {
                constexpr xyz_pos_t vec { 0, 0, fade_height + epsilon };
                CHECK_THAT(to_native_pos(to_machine_pos(vec)).z, Catch::Matchers::WithinAbs(vec.z, tolerance));
            }
        }

        SECTION("Above fade") {
            CHECK(to_machine_pos(xyz_pos_t { 0, 0, 15 }) == MachinePosXYZ { 0, 0, 15 });
            CHECK(to_native_pos(MachinePosXYZ { 0, 0, 15 }) == xyz_pos_t { 0, 0, 15 });
        }

        SECTION("Full fade") {
            CHECK_THAT(to_machine_pos(xyz_pos_t { 0, 0, 0 }).z, Catch::Matchers::WithinAbs(z_correction, tolerance));
            CHECK_THAT(to_native_pos(MachinePosXYZ { 0, 0, z_correction }).z, Catch::Matchers::WithinAbs(0, tolerance));
        }

        SECTION("Half fade") {
            const float machine_z = 5 + 0.5f * z_correction;
            CHECK_THAT(to_machine_pos(xyz_pos_t { 0, 0, 5 }).z, Catch::Matchers::WithinAbs(machine_z, tolerance));
            CHECK_THAT(to_native_pos(MachinePosXYZ { 0, 0, machine_z }).z, Catch::Matchers::WithinAbs(5, tolerance));
        }

        SECTION("1/3 fade") {
            const float native_z = 10.f / 3;
            const float machine_z = native_z + 2.f / 3 * z_correction;
            CHECK_THAT(to_machine_pos(xyz_pos_t { 0, 0, native_z }).z, Catch::Matchers::WithinAbs(machine_z, tolerance));
            CHECK_THAT(to_native_pos(MachinePosXYZ { 0, 0, machine_z }).z, Catch::Matchers::WithinAbs(native_z, tolerance));
        }

        SECTION("z < 0") {
            CHECK(to_machine_pos(xyz_pos_t { 0, 0, -10 }).z == -10 + z_correction);
            CHECK(to_native_pos(MachinePosXYZ { 0, 0, -10 + z_correction }).z == -10);
        }
    }

    SECTION("Monotonicity & Round trip") {
        const float z_correction = GENERATE(0.0f, 0.013f, -0.013f, 0.47f, -0.47f, 1.73f, -1.73f);
        UBL::z_correction = z_correction;

        const float fade_height = GENERATE(5.0f, 9.81f, 10.0f);
        Planner::set_z_fade_height(fade_height);

        CAPTURE(z_correction, fade_height);

        constexpr float z_start = -10;

        float prev_to_machine = to_machine_pos({ 0, 0, z_start }).z;
        float prev_to_native = to_native_pos({ 0, 0, z_start }).z;

        for (float z = z_start; z < fade_height + 1; z += 0.0001f) {
            CAPTURE(z);

            const float to_machine = to_machine_pos({ 0, 0, z }).z;
            CHECK(to_machine >= prev_to_machine);
            prev_to_machine = to_machine;

            const float to_native = to_native_pos({ 0, 0, z }).z;
            CHECK(to_native >= prev_to_native);
            prev_to_native = to_native;

            // There are two solutions in the area about fade_height, so the roundtrip there does not apply
            if (std::abs(z - fade_height) > std::abs(z_correction)) {
                const float to_native_round_trip = to_native_pos({ 0, 0, to_machine }).z;
                CHECK_THAT(to_native_round_trip, Catch::Matchers::WithinAbs(z, roundtrip_tolerance));

                const float to_machine_round_trip = to_machine_pos({ 0, 0, to_native }).z;
                CHECK_THAT(to_machine_round_trip, Catch::Matchers::WithinAbs(z, roundtrip_tolerance));
            }
        }
    }
}
