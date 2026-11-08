#include <module/motion.h>
#include <module/planner.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

constexpr float epsilon = std::numeric_limits<float>::epsilon();
constexpr float tolerance = 0.000001f;
constexpr float roundtrip_tolerance = std::sqrt(tolerance);

/// Sets all the inputs of the conversion to "no correction"
static void reset_corrections() {
    Planner::set_z_fade_height(0);
    UBL::z_correction = 0;
    UBL::slope = {};
    Planner::skew_factor = { 0, 0, 0 };
    Planner::skew_disabled = 0;
}

TEST_CASE("machine_pos_conv") {
    reset_corrections();

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

namespace {

/// Machine to physical position for a printer with skewed axes, written out independently from the
/// code under test (Marlin's unskew() without the edge check). With correct factors, physical == native.
xyz_pos_t physical_from_machine(const MachinePosXYZ &m, const skew_factor_t &f) {
    return { m.x + m.y * f.xy + m.z * f.xz, m.y + m.z * f.yz, m.z };
}

void check_near(const xyz_pos_t &actual, const xyz_pos_t &expected, float tol) {
    CAPTURE(actual.x, actual.y, actual.z, expected.x, expected.y, expected.z);
    CHECK_THAT(actual.x, Catch::Matchers::WithinAbs(expected.x, tol));
    CHECK_THAT(actual.y, Catch::Matchers::WithinAbs(expected.y, tol));
    CHECK_THAT(actual.z, Catch::Matchers::WithinAbs(expected.z, tol));
}

/// Factors from a Calilantern print on a CORE One L (M852 I J K)
constexpr skew_factor_t calilantern_factors { -0.0000997f, -0.0000997f, -0.0010670f };

constexpr float lim = SKEW_FACTOR_MAX;

/// All sign combinations at the M852 limit, and the Calilantern factors
constexpr skew_factor_t limit_factors[] = {
    { lim, lim, lim }, { lim, lim, -lim }, { lim, -lim, lim }, { lim, -lim, -lim },
    { -lim, lim, lim }, { -lim, lim, -lim }, { -lim, -lim, lim }, { -lim, -lim, -lim },
    calilantern_factors
};

} // namespace

TEST_CASE("machine_pos_conv skew off") {
    reset_corrections();

    // No skew and no mesh: both conversions are an exact identity
    for (const xyz_pos_t pos : { xyz_pos_t { 0, 0, 0 }, xyz_pos_t { -2, -8, 0.2f }, xyz_pos_t { 302, 300, 338 }, xyz_pos_t { 123.456f, 78.9f, 12.3f } }) {
        CAPTURE(pos.x, pos.y, pos.z);
        CHECK(to_machine_pos(pos) == pos.to_tag<MachinePosTag>());
        CHECK(to_native_pos(pos.to_tag<MachinePosTag>()) == pos);
    }

    // Skew disabled (homing): no change, even with factors set
    Planner::skew_factor = { lim, lim, lim };
    Planner::skew_disabled = 1;
    CHECK(!Planner::skew_active());
    CHECK(to_machine_pos(xyz_pos_t { 100, 50, 200 }) == MachinePosXYZ { 100, 50, 200 });
    CHECK(to_native_pos(MachinePosXYZ { 100, 50, 200 }) == xyz_pos_t { 100, 50, 200 });
}

TEST_CASE("machine_pos_conv skew known values") {
    reset_corrections();

    // Hand-computed with Marlin's skew(): x -= y*I + z*(J - I*K), y -= z*K
    SECTION("XY") {
        Planner::skew_factor = { 0.005f, 0, 0 };
        check_near(to_machine_pos(xyz_pos_t { 100, 50, 0 }), MachinePosXYZ { 99.75f, 50, 0 }, tolerance * 100);
        check_near(to_native_pos(MachinePosXYZ { 99.75f, 50, 0 }), xyz_pos_t { 100, 50, 0 }, tolerance * 100);
    }

    SECTION("XZ") {
        Planner::skew_factor = { 0, 0.002f, 0 };
        check_near(to_machine_pos(xyz_pos_t { 100, 50, 200 }), MachinePosXYZ { 99.6f, 50, 200 }, tolerance * 100);
        check_near(to_native_pos(MachinePosXYZ { 99.6f, 50, 200 }), xyz_pos_t { 100, 50, 200 }, tolerance * 100);
    }

    SECTION("YZ") {
        Planner::skew_factor = { 0, 0, -0.001f };
        check_near(to_machine_pos(xyz_pos_t { 100, 50, 200 }), MachinePosXYZ { 100, 50.2f, 200 }, tolerance * 100);
        check_near(to_native_pos(MachinePosXYZ { 100, 50.2f, 200 }), xyz_pos_t { 100, 50, 200 }, tolerance * 100);
    }

    SECTION("All three, with the I*K cross term") {
        // x = 100 - 50*0.005 - 200*(0.002 - 0.005*(-0.001)) = 100 - 0.25 - 0.401 = 99.349
        Planner::skew_factor = { 0.005f, 0.002f, -0.001f };
        check_near(to_machine_pos(xyz_pos_t { 100, 50, 200 }), MachinePosXYZ { 99.349f, 50.2f, 200 }, tolerance * 100);
        check_near(to_native_pos(MachinePosXYZ { 99.349f, 50.2f, 200 }), xyz_pos_t { 100, 50, 200 }, tolerance * 100);
    }

    SECTION("The machine puts the nozzle at the native position") {
        Planner::skew_factor = calilantern_factors;
        for (const xyz_pos_t pos : { xyz_pos_t { 150, 150, 100 }, xyz_pos_t { 5, 290, 300 }, xyz_pos_t { 295, 5, 0.2f } }) {
            CAPTURE(pos.x, pos.y, pos.z);
            check_near(physical_from_machine(to_machine_pos(pos), calilantern_factors), pos, 1e-4f);
        }
    }
}

TEST_CASE("machine_pos_conv skew edge check (as in Marlin)") {
    reset_corrections();

    SECTION("No skew if the native X is less than X_MIN_POS + 1") {
        Planner::skew_factor = { lim, 0, 0 };
        CHECK(to_machine_pos(xyz_pos_t { -1.5f, 100, 0 }) == MachinePosXYZ { -1.5f, 100, 0 });
    }

    SECTION("No skew if the native Y is less than Y_MIN_POS + 1") {
        Planner::skew_factor = { 0, 0, lim };
        CHECK(to_machine_pos(xyz_pos_t { 100, -7.5f, 100 }) == MachinePosXYZ { 100, -7.5f, 100 });
    }

    SECTION("No skew if the result is out of the limits") {
        // x = 302 + 100*0.005 = 302.5 > X_MAX_POS
        Planner::skew_factor = { -lim, 0, 0 };
        CHECK(to_machine_pos(xyz_pos_t { 302, 100, 0 }) == MachinePosXYZ { 302, 100, 0 });

        // The other sign stays in the limits, so the skew applies: x = 302 - 0.5
        Planner::skew_factor = { lim, 0, 0 };
        check_near(to_machine_pos(xyz_pos_t { 302, 100, 0 }), MachinePosXYZ { 301.5f, 100, 0 }, tolerance * 100);

        // y = 300 + 300*0.001 = 300.3 > Y_MAX_POS
        Planner::skew_factor = { 0, 0, -0.001f };
        CHECK(to_machine_pos(xyz_pos_t { 150, 300, 300 }) == MachinePosXYZ { 150, 300, 300 });
    }

    SECTION("In the edge band, the round trip is not exact") {
        // skew() does nothing (x < X_MIN_POS + 1), but unskew() applies: x = -1.5 + 100*0.005 = -1
        Planner::skew_factor = { lim, 0, 0 };
        const MachinePosXYZ machine = to_machine_pos(xyz_pos_t { -1.5f, 100, 0 });
        check_near(to_native_pos(machine), xyz_pos_t { -1.0f, 100, 0 }, tolerance * 100);
    }
}

TEST_CASE("machine_pos_conv skew keeps the machine XY in the limits") {
    // A native target in the X and Y limits never gives a machine position out of the limits
    // (G123 and other moves that do not clamp in machine coordinates rely on this).
    reset_corrections();
    UBL::z_correction = 0.47f;
    UBL::slope = { { { 0.001f, -0.0005f } } };

    for (const skew_factor_t &factors : limit_factors) {
        Planner::skew_factor = factors;
        CAPTURE(factors.xy, factors.xz, factors.yz);

        for (float x = X_MIN_POS; x <= X_MAX_POS; x += 0.25f) {
            for (float y = Y_MIN_POS; y <= Y_MAX_POS; y += 0.25f) {
                for (const float z : { 0.f, 0.2f, 100.f, 338.f }) {
                    const MachinePosXYZ machine = to_machine_pos(xyz_pos_t { x, y, z });
                    if (machine.x < X_MIN_POS || machine.x > X_MAX_POS || machine.y < Y_MIN_POS || machine.y > Y_MAX_POS) {
                        CAPTURE(x, y, z, machine.x, machine.y);
                        FAIL("machine position out of the limits");
                    }
                }
            }
        }
    }
}

TEST_CASE("machine_pos_conv skew with MBL") {
    reset_corrections();

    // Mesh: 0.1 + 0.01*x - 0.002*y. A slope makes the place of the mesh lookup visible.
    UBL::z_correction = 0.1f;
    UBL::slope = { { { 0.01f, -0.002f } } };
    Planner::skew_factor = { 0.005f, 0, 0 };

    SECTION("Fade off: mesh is looked up at the skewed XY") {
        // machine XY = (99.75, 50), mesh there = 0.1 + 0.9975 - 0.1 = 0.9975 (at native XY it would be 1.0)
        check_near(to_machine_pos(xyz_pos_t { 100, 50, 0.2f }), MachinePosXYZ { 99.75f, 50, 1.1975f }, tolerance * 10);
        check_near(to_native_pos(MachinePosXYZ { 99.75f, 50, 1.1975f }), xyz_pos_t { 100, 50, 0.2f }, tolerance * 10);
    }

    SECTION("Half fade") {
        Planner::set_z_fade_height(10);
        // z = 5 + 0.5 * 0.9975
        check_near(to_machine_pos(xyz_pos_t { 100, 50, 5 }), MachinePosXYZ { 99.75f, 50, 5.49875f }, tolerance * 10);
        check_near(to_native_pos(MachinePosXYZ { 99.75f, 50, 5.49875f }), xyz_pos_t { 100, 50, 5 }, tolerance * 10);
    }

    SECTION("Above fade: no MBL, skew still applies") {
        Planner::set_z_fade_height(10);
        Planner::skew_factor = { 0.005f, 0, 0.001f };
        // y = 50 - 15*0.001, x = 100 - 0.25 - 15*(0 - 0.005*0.001)
        check_near(to_machine_pos(xyz_pos_t { 100, 50, 15 }), MachinePosXYZ { 99.750075f, 49.985f, 15 }, tolerance * 100);
        check_near(to_native_pos(MachinePosXYZ { 99.750075f, 49.985f, 15 }), xyz_pos_t { 100, 50, 15 }, tolerance * 100);
    }
}

TEST_CASE("machine_pos_conv skew round trip") {
    // Away from the edge band, the two conversions are exact inverses
    reset_corrections();

    const float z_correction = GENERATE(0.0f, 0.47f, -0.47f);
    const float fade_height = GENERATE(0.0f, 10.0f);
    UBL::z_correction = z_correction;
    UBL::slope = { { { 0.001f, -0.0005f } } };
    Planner::set_z_fade_height(fade_height);

    for (const skew_factor_t &factors : limit_factors) {
        Planner::skew_factor = factors;
        CAPTURE(factors.xy, factors.xz, factors.yz, z_correction, fade_height);

        for (const float x : { 5.f, 150.f, 290.f }) {
            for (const float y : { 5.f, 150.f, 290.f }) {
                for (const float z : { 0.f, 0.2f, 5.f, 9.9f, 10.f, 50.f, 338.f }) {
                    xyz_pos_t native;
                    native.x = x;
                    native.y = y;
                    native.z = z;
                    const MachinePosXYZ machine = to_machine_pos(native);
                    CAPTURE(x, y, z);

                    // Near the fade height, the fade has two solutions (see the test above), so skip it
                    const float mbl = ubl.get_z_correction(machine.xy());
                    if (fade_height != 0 && std::abs(z - fade_height) <= std::abs(mbl)) {
                        continue;
                    }

                    check_near(to_native_pos(machine), native, 1e-4f);

                    const MachinePosXYZ machine_in = native.to_tag<MachinePosTag>();
                    check_near(to_machine_pos(to_native_pos(machine_in)), machine_in, 1e-4f);
                }
            }
        }
    }
}
