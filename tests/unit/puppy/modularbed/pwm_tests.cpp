#include "test_support.hpp"

#include "PuppyConfig.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

using namespace modularbed;
using Catch::Approx;
using fake::world;

namespace {

constexpr uint32_t all_bedlets = HEATBEDLET_COUNT;

/// Number of pulses of a branch that are on in each of the 256 steps of the period.
std::array<int, PWM_PERIOD_LENGTH> pulses_on(int branch) {
    std::array<int, PWM_PERIOD_LENGTH> on {};
    for (const fake::Pulse &p : world.active_pulses) {
        if (fake::branch_of(p.bedlet) != branch) {
            continue;
        }
        for (uint32_t i = 0; i < p.length; i++) {
            on[(p.start + i) % PWM_PERIOD_LENGTH]++;
        }
    }
    return on;
}

void set_pwm(const std::array<float, all_bedlets> &pwm) {
    for (uint32_t i = 0; i < all_bedlets; i++) {
        test::bedlet(i).m_PWMValue = pwm[i];
    }
}

} // namespace

TEST_CASE("modularbed pwm: the pulses of a branch overlap as little as possible") {
    test::boot();
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> pwm_dist(0.0f, 1.0f);
    std::bernoulli_distribution off_dist(0.3);

    for (int round = 0; round < 2000; round++) {
        std::array<float, all_bedlets> pwm;
        for (float &p : pwm) {
            p = off_dist(rng) ? 0.0f : pwm_dist(rng);
        }
        if (round == 0) {
            pwm.fill(1.0f);
        }
        set_pwm(pwm);
        PWMLogic::ApplyPWMValuesWithoutLimiters();

        // One pulse of the correct length for each bedlet
        for (uint32_t i = 0; i < all_bedlets; i++) {
            CAPTURE(round, i, pwm[i]);
            const auto expected = static_cast<uint32_t>(pwm[i] * PWM_PERIOD_LENGTH);
            REQUIRE(fake::duty(i) == Approx(static_cast<float>(expected) / PWM_PERIOD_LENGTH));
            REQUIRE(std::count_if(world.active_pulses.begin(), world.active_pulses.end(), [&](const auto &p) { return p.bedlet == i; }) == (expected > 0 ? 1 : 0));
        }

        for (int branch = 0; branch < 2; branch++) {
            uint32_t sum = 0;
            for (const fake::Pulse &p : world.active_pulses) {
                if (fake::branch_of(p.bedlet) == branch) {
                    sum += p.length;
                }
            }
            const auto on = pulses_on(branch);
            const int most = *std::max_element(on.begin(), on.end());
            const int least = *std::min_element(on.begin(), on.end());
            CAPTURE(round, branch, sum);
            // The pulses fill the period one after the other: in each step, the number of
            // heaters that are on is the floor or the ceiling of the average.
            REQUIRE(most <= static_cast<int>((sum + PWM_PERIOD_LENGTH - 1) / PWM_PERIOD_LENGTH));
            REQUIRE(least >= static_cast<int>(sum / PWM_PERIOD_LENGTH));
        }
    }
}

TEST_CASE("modularbed pwm: limits of the heater and the branch current") {
    test::boot();
    std::array<float, all_bedlets> pwm;
    pwm.fill(1.0f);

    SECTION("each heater is limited to its maximum current") {
        // Only one heater on: the branch limit does not apply.
        pwm.fill(0.0f);
        pwm[3] = 1.0f;
        set_pwm(pwm);
        PWMLogic::ApplyPWMValues();
        // 13 ohm at 22 °C gives 1.83 A. The default limit is 1.5 A.
        CHECK(fake::branch_current(1) == Approx(HEATBEDLET_DEFAULT_MAX_ALLOWED_CURRENT).margin(0.01));
    }

    SECTION("a branch is limited to the current of the power supply") {
        set_pwm(pwm);
        PWMLogic::ApplyPWMValues();
        // The first step limits the current to the start of the ramp, plus one ramp step.
        CHECK(fake::branch_current(0) > PWM_MAX_CURRENT_AMPS[Branch::A] * PWM_RAMPING_START);
        CHECK(fake::branch_current(0) < PWM_MAX_CURRENT_AMPS[Branch::A] * PWM_RAMPING_END);
        CHECK(fake::branch_current(1) > PWM_MAX_CURRENT_AMPS[Branch::B] * PWM_RAMPING_START);
        CHECK(fake::branch_current(1) < PWM_MAX_CURRENT_AMPS[Branch::B] * PWM_RAMPING_END);
        CHECK(PWMLogic::GetExpectedCurrent(Branch::A) == Approx(fake::branch_current(0)).margin(0.05));
        CHECK(PWMLogic::GetExpectedCurrent(Branch::B) == Approx(fake::branch_current(1)).margin(0.05));
    }

    SECTION("with no limiters, all heaters are fully on") {
        set_pwm(pwm);
        PWMLogic::ApplyPWMValuesWithoutLimiters();
        for (uint32_t i = 0; i < all_bedlets; i++) {
            CHECK(fake::duty(i) == 1.0f);
        }
    }
}

TEST_CASE("modularbed pwm: a disabled driver gives no pulses") {
    test::boot();
    std::array<float, all_bedlets> pwm;
    pwm.fill(0.5f);
    set_pwm(pwm);
    PWMLogic::DisablePWM();
    CHECK(world.active_pulses.empty());
    PWMLogic::EnablePWM();
    CHECK(world.active_pulses.size() == all_bedlets);
}

TEST_CASE("modularbed pwm: the branch current stays below the limit during a heat-up") {
    test::boot();
    test::write_all_targets(110);
    float most[2] = { 0, 0 };
    for (int step = 0; step < 600; step++) {
        test::run_ms(100, true);
        for (int branch = 0; branch < 2; branch++) {
            most[branch] = std::max(most[branch], fake::branch_current(branch));
        }
    }
    CHECK(most[0] <= PWM_MAX_CURRENT_AMPS[Branch::A]);
    CHECK(most[1] <= PWM_MAX_CURRENT_AMPS[Branch::B]);
    // The ramp uses most of the available current.
    CHECK(most[0] > PWM_MAX_CURRENT_AMPS[Branch::A] * PWM_RAMPING_START);
    CHECK(most[1] > PWM_MAX_CURRENT_AMPS[Branch::B] * PWM_RAMPING_START);
}

TEST_CASE("modularbed pwm: a measured current above the threshold lowers the PWM at once") {
    test::boot();
    test::write_all_targets(110);
    test::run_ms(20'000, true);
    const float before = fake::branch_current(0);
    REQUIRE(before > PWM_MAX_CURRENT_AMPS[Branch::A] * PWM_RAMPING_START);

    // For example a cold steel sheet on a hot bed: the real current is higher than the model.
    world.extra_current[0] = PWM_MAX_CURRENT_AMPS[Branch::A] * PWM_OVERCURRENT_THRESHOLD - before + 0.3f;
    test::run_ms(1500, true);
    CHECK(fake::branch_current(0) - world.extra_current[0] < before * (PWM_RAMPING_START + 0.05f));
}
