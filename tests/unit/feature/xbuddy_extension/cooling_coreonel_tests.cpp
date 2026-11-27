#include <feature/xbuddy_extension/cooling.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>

using namespace buddy;

namespace {

/// The values of xbuddy_extension.cpp: the regulation step, then the overrides.
struct Loop {
    FanCooling cooling;
    FanCooling::FanPWM max_auto_pwm { 255 };

    FanCooling::FanPWM step(Temperature current, std::optional<Temperature> target, PWM255OrAuto target_pwm, bool spinning = true) {
        return cooling.apply_pwm_overrides(spinning, cooling.compute_pwm_step(current, target, target_pwm, max_auto_pwm));
    }
};

} // namespace

TEST_CASE("chamber cooling (CORE One L): the limits", "[chamber]") {
    STATIC_CHECK(FanCooling::recovery_temp == 60);
    STATIC_CHECK(FanCooling::overheating_temp == 65);
    STATIC_CHECK(FanCooling::critical_temp == 68);
    STATIC_CHECK(FanCooling::recovery_temp < FanCooling::overheating_temp);
    STATIC_CHECK(FanCooling::overheating_temp < FanCooling::critical_temp);
}

TEST_CASE("chamber cooling (CORE One L): full power at the overheating temperature in each mode", "[chamber]") {
    const bool legacy = GENERATE(true, false);
    const PWM255OrAuto target_pwm = GENERATE(PWM255OrAuto { pwm_auto }, PWM255OrAuto { PWM255 { 0 } }, PWM255OrAuto { PWM255 { 30 } });
    const std::optional<Temperature> target = GENERATE(std::optional<Temperature> {}, std::optional<Temperature> { 40 }, std::optional<Temperature> { 60 });
    CAPTURE(legacy, target_pwm.value_or(PWM255 { 255 }).value, target.value_or(-1));

    Loop loop;
    loop.cooling.regulator_legacy = legacy;
    loop.max_auto_pwm = PWM255 { 0 };
    CHECK(loop.step(FanCooling::overheating_temp, target, target_pwm, false) == FanCooling::max_pwm);
    CHECK(loop.cooling.get_overheating_temp_flag());
    CHECK_FALSE(loop.cooling.get_critical_temp_flag());

    // Full power stays on down to the recovery temperature.
    for (Temperature t = FanCooling::overheating_temp; t >= FanCooling::recovery_temp; t -= 0.5f) {
        CHECK(loop.step(t, target, target_pwm) == FanCooling::max_pwm);
    }
    std::ignore = loop.step(FanCooling::recovery_temp - 0.1f, target, target_pwm);
    CHECK_FALSE(loop.cooling.get_overheating_temp_flag());

    CHECK(loop.step(FanCooling::critical_temp, target, target_pwm) == FanCooling::max_pwm);
    CHECK(loop.cooling.get_critical_temp_flag());
}

TEST_CASE("chamber cooling (CORE One L): auto cooling reaches the maximum above the target", "[chamber]") {
    const bool legacy = GENERATE(true, false);
    CAPTURE(legacy);

    Loop loop;
    loop.cooling.regulator_legacy = legacy;
    loop.max_auto_pwm = PWM255 { 200 };
    FanCooling::FanPWM pwm { 0 };
    for (int s = 0; s < 120; ++s) {
        pwm = loop.step(55, 35, pwm_auto);
    }
    CHECK(pwm == loop.max_auto_pwm);

    // Under the target, the legacy regulator returns to 0.
    if (legacy) {
        for (int s = 0; s < 300; ++s) {
            pwm = loop.step(30, 35, pwm_auto);
        }
        CHECK(pwm == PWM255 { 0 });
    }
}

TEST_CASE("chamber cooling (CORE One L): the auto PWM is in its range for all inputs", "[chamber]") {
    const bool legacy = GENERATE(true, false);
    CAPTURE(legacy);

    Loop loop;
    loop.cooling.regulator_legacy = legacy;
    loop.max_auto_pwm = PWM255 { 180 };
    for (Temperature target = -10; target <= 70; target += 7.5f) {
        for (Temperature current = 0; current < FanCooling::overheating_temp; current += 1.25f) {
            const auto pwm = loop.cooling.compute_pwm_step(current, target, pwm_auto, loop.max_auto_pwm);
            CAPTURE(target, current);
            CHECK(pwm.value <= loop.max_auto_pwm.value);
        }
    }
}
