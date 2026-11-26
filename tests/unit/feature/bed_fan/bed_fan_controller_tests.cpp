#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include <feature/bed_fan/bed_fan.hpp>
#include <feature/bed_fan/controller.hpp>
#include <feature/chamber/chamber.hpp>
#include <module/temperature.h>

namespace bed_fan {
BedFan &bed_fan() {
    static BedFan instance;
    return instance;
}
} // namespace bed_fan

namespace buddy {
Chamber &chamber() {
    static Chamber instance;
    return instance;
}
} // namespace buddy

using AutomaticMode = bed_fan::Controller::AutomaticMode;

namespace {

constexpr uint8_t min_pwm = 25;

void set_bed(float current, int16_t target) {
    Temperature::bed_current = current;
    Temperature::bed_target = target;
    buddy::chamber().current.reset();
    buddy::chamber().target.reset();
}

void set_chamber(float current, float target) {
    buddy::chamber().current = current;
    buddy::chamber().target = target;
}

} // namespace

TEST_CASE("bed_fan automatic mode interpolates inside the threshold") {
    const AutomaticMode mode { .max_pwm = 255, .bed_temp_threshold = 10, .chamber_temp_threshold = -1 };

    set_bed(55, 60); // scale 0.5
    CHECK(mode.calculate_pwm() == 140);

    set_bed(60, 60); // scale 0
    CHECK(mode.calculate_pwm() == min_pwm);

    set_bed(70, 60); // bed above target
    CHECK(mode.calculate_pwm() == min_pwm);
}

TEST_CASE("bed_fan automatic mode gives max_pwm above the threshold") {
    // M106 P5 R A255 B10, and the PrusaSlicer CORE One L start G-code M106 P5 R A125 B10.
    for (const uint8_t max_pwm : { uint8_t(255), uint8_t(200), uint8_t(125) }) {
        const AutomaticMode mode { .max_pwm = max_pwm, .bed_temp_threshold = 10, .chamber_temp_threshold = -1 };
        // Each difference from the threshold to a cold bed at a high target.
        for (int16_t diff = 10; diff <= 100; ++diff) {
            set_bed(20, static_cast<int16_t>(20 + diff));
            INFO("A" << int(max_pwm) << " B10, difference " << diff);
            CHECK(mode.calculate_pwm() == max_pwm);
        }
    }
}

TEST_CASE("bed_fan automatic mode gives max_pwm above the chamber threshold") {
    // M106 P5 R A125 B10 C5: the chamber has priority.
    const AutomaticMode mode { .max_pwm = 125, .bed_temp_threshold = 10, .chamber_temp_threshold = 5 };
    for (float diff = 5; diff <= 40; diff += 0.5f) {
        set_bed(100, 100);
        set_chamber(20, 20 + diff);
        INFO("chamber difference " << diff);
        CHECK(mode.calculate_pwm() == 125);
    }
}

TEST_CASE("bed_fan automatic mode with max_pwm at or below the minimum") {
    const AutomaticMode mode { .max_pwm = 10, .bed_temp_threshold = 10, .chamber_temp_threshold = -1 };
    set_bed(20, 100);
    CHECK(mode.calculate_pwm() == min_pwm);
}

TEST_CASE("bed_fan automatic mode uses the bed if the chamber has no values") {
    const AutomaticMode mode { .max_pwm = 255, .bed_temp_threshold = 10, .chamber_temp_threshold = 5 };
    set_bed(55, 60); // the chamber values are empty, scale 0.5
    CHECK(mode.calculate_pwm() == 140);
}

TEST_CASE("bed_fan automatic mode: the chamber has priority also at the minimum") {
    const AutomaticMode mode { .max_pwm = 255, .bed_temp_threshold = 10, .chamber_temp_threshold = 5 };
    set_bed(20, 100);
    set_chamber(50, 40); // chamber hotter than its target
    CHECK(mode.calculate_pwm() == min_pwm);
}

TEST_CASE("bed_fan automatic mode with the bed heater off") {
    const AutomaticMode mode { .max_pwm = 255, .bed_temp_threshold = 10, .chamber_temp_threshold = -1 };
    set_bed(80, 0);
    CHECK(mode.calculate_pwm() == min_pwm);
}

TEST_CASE("bed_fan automatic mode gives min_pwm for a NaN scale") {
    // std::clamp would let NaN through to the conversion to uint8_t, which is undefined.
    const AutomaticMode mode { .max_pwm = 255, .bed_temp_threshold = 10, .chamber_temp_threshold = 5 };
    set_bed(20, 100);
    set_chamber(NAN, 40);
    CHECK(mode.calculate_pwm() == min_pwm);
}
