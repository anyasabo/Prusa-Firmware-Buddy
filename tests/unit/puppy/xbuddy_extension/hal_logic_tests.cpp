/// Host tests of the pure logic of the xBuddy extension puppy: the chamber
/// thermistor, the Modbus register encoding, the fan tachometer and the PWM
/// timer prescaler.
#include "hal_math.hpp"
#include "register_encoding.hpp"
#include "temperature.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <utils/timing/timer_event_period_tracker.hpp>

using register_encoding::rpm_register;
using register_encoding::tenths_register;

namespace {

/// Raw ADC value of the 104NT thermistor with the 33k pull-up at \p r_kohm.
uint16_t raw_at(float r_kohm) {
    const float r = r_kohm * 1000.0f;
    return static_cast<uint16_t>(std::round(r * 4095 / (r + 33000)));
}

/// The main board (`XBuddyExtension::step()`) stops with MINTEMP at or below
/// 0 °C and with MAXTEMP above 85 °C. It reads the register as unsigned.
constexpr float main_board_chamber_temp(uint16_t reg) { return static_cast<float>(reg) / 10.0f; }
constexpr bool main_board_mintemp(uint16_t reg) { return main_board_chamber_temp(reg) <= 0.0f; }
constexpr bool main_board_maxtemp(uint16_t reg) { return main_board_chamber_temp(reg) > 85.0f; }

} // namespace

TEST_CASE("xbe thermistor: the table points", "[xbe][temperature]") {
    struct Point {
        float celsius;
        float r_kohm;
    };
    const auto point = GENERATE(Point { -10, 623.2f }, Point { 0, 354.6f }, Point { 10, 208.8f }, Point { 25, 100 }, Point { 40, 50.9f }, Point { 50, 33.45f }, Point { 60, 22.48f }, Point { 80, 10.8f }, Point { 85, 9.094f }, Point { 100, 5.569f });
    CAPTURE(point.celsius);
    CHECK(temperature::raw_to_celsius(raw_at(point.r_kohm)) == Catch::Approx(point.celsius).margin(0.15));
}

TEST_CASE("xbe thermistor: the whole ADC range is monotonic and finite", "[xbe][temperature]") {
    float prev = temperature::raw_to_celsius(0);
    for (uint32_t raw = 1; raw <= 4095; ++raw) {
        const float c = temperature::raw_to_celsius(static_cast<uint16_t>(raw));
        REQUIRE(std::isfinite(c));
        REQUIRE(c < prev);
        prev = c;
    }
}

TEST_CASE("xbe thermistor: one LSB of ADC noise is small in the chamber range", "[xbe][temperature]") {
    for (uint16_t raw = raw_at(33.45f /* 50 °C */); raw < raw_at(208.8f /* 10 °C */); ++raw) {
        CAPTURE(raw);
        REQUIRE(temperature::raw_to_celsius(raw) - temperature::raw_to_celsius(raw + 1) < 0.1f);
    }
}

TEST_CASE("xbe thermistor: a broken thermistor stops the main board", "[xbe][temperature]") {
    SECTION("Open (cable unplugged): the ADC reads full scale") {
        const float c = temperature::raw_to_celsius(4095);
        CHECK(c < -50.0f);
        const uint16_t reg = tenths_register(c);
        CHECK(reg == 0);
        CHECK(main_board_mintemp(reg));
    }

    SECTION("Short circuit: the ADC reads 0") {
        // The HAL also reports ADC 0 until its first ADC sample.
        const float c = temperature::raw_to_celsius(0);
        CHECK(c > 120.0f);
        const uint16_t reg = tenths_register(c);
        CHECK(main_board_maxtemp(reg));
    }

    SECTION("Room temperature is neither") {
        const uint16_t reg = tenths_register(temperature::raw_to_celsius(raw_at(100)));
        CHECK_FALSE(main_board_mintemp(reg));
        CHECK_FALSE(main_board_maxtemp(reg));
    }
}

TEST_CASE("xbe registers: tenths", "[xbe][registers]") {
    CHECK(tenths_register(23.5f) == 235);
    CHECK(tenths_register(0.0f) == 0);
    CHECK(tenths_register(0.09f) == 0);
    CHECK(tenths_register(0.1f) == 1);
    CHECK(tenths_register(84.99f) == 849);
    CHECK(tenths_register(6553.4f) == 65534);

    SECTION("Saturates at the top") {
        CHECK(tenths_register(6553.5f) == 65535);
        CHECK(tenths_register(6553.6f) == 65535);
        CHECK(tenths_register(1e9f) == 65535);
        CHECK(tenths_register(std::numeric_limits<float>::infinity()) == 65535);
    }

    SECTION("Negative values and NaN give 0, as the Cortex-M33 conversion did") {
        CHECK(tenths_register(-0.05f) == 0);
        CHECK(tenths_register(-56.4f) == 0);
        CHECK(tenths_register(-1e9f) == 0);
        CHECK(tenths_register(-std::numeric_limits<float>::infinity()) == 0);
        CHECK(tenths_register(std::numeric_limits<float>::quiet_NaN()) == 0);
    }
}

namespace {

/// A model of TIM1 (16-bit, 1 MHz) and the IRQ handler for one tachometer
/// channel. Time is in timer ticks and does not wrap.
class TachModel {
public:
    TimerEventPeriodTracker tracker;
    uint64_t now = 0;
    uint64_t next_overflow = 0x1'0000;

    /// Advance to \p t and process the overflows before it.
    void advance_to(uint64_t t) {
        while (next_overflow <= t) {
            tracker.handle_multi_event(0, false, true);
            next_overflow += 0x1'0000;
        }
        now = t;
    }

    /// A tachometer edge at \p t.
    void edge_at(uint64_t t) {
        // An overflow at the same tick as the edge comes in the same IRQ.
        while (next_overflow < t) {
            tracker.handle_multi_event(0, false, true);
            next_overflow += 0x1'0000;
        }
        const bool same_irq_overflow = next_overflow == t;
        if (same_irq_overflow) {
            next_overflow += 0x1'0000;
        }
        tracker.handle_multi_event(static_cast<uint16_t>(t & 0xFFFF), true, same_irq_overflow);
        now = t;
    }

    uint32_t rpm() const { return hal::math::tach_period_to_rpm(tracker.period_unsafe()); }

    /// Run a fan at \p rpm for \p edges tachometer edges.
    void run(uint32_t rpm, uint32_t edges) {
        const uint64_t period = 30'000'000 / rpm;
        for (uint32_t i = 0; i < edges; ++i) {
            edge_at(now + period);
        }
    }
};

/// Time in ticks after which the tracker gives up: 16 timer overflows.
constexpr uint64_t stall_ticks = uint64_t { TimerEventPeriodTracker::max_timer_overflows } << 16;

} // namespace

TEST_CASE("xbe tach: period to RPM", "[xbe][tach]") {
    CHECK(hal::math::tach_period_to_rpm(TimerEventPeriodTracker::invalid_period) == 0);
    CHECK(hal::math::tach_period_to_rpm(0) == 0);
    CHECK(hal::math::tach_period_to_rpm(1) == 30'000'000);
    CHECK(hal::math::tach_period_to_rpm(5000) == 6000);
    CHECK(hal::math::tach_period_to_rpm(30'000'000) == 1);
    CHECK(hal::math::tach_period_to_rpm(30'000'001) == 0);
}

TEST_CASE("xbe tach: a fan that is not connected reads 0", "[xbe][tach]") {
    TachModel tach;
    CHECK(tach.rpm() == 0);
    tach.advance_to(10 * stall_ticks);
    CHECK(tach.rpm() == 0);
}

TEST_CASE("xbe tach: a fan at a constant speed", "[xbe][tach]") {
    const uint32_t rpm = GENERATE(31u, 100u, 500u, 1000u, 2100u, 3000u, 5400u, 6700u, 10000u);
    CAPTURE(rpm);
    TachModel tach;
    tach.advance_to(12345);
    tach.edge_at(tach.now);
    CHECK(tach.rpm() == 0); // One edge is not a period
    // Many timer overflows, so that the 16-bit counter wraps often
    tach.run(rpm, 2000);
    const uint32_t expected = 30'000'000 / (30'000'000 / rpm);
    CHECK(tach.rpm() == expected);
}

TEST_CASE("xbe tach: a stalled fan reads 0 after 16 timer overflows", "[xbe][tach]") {
    TachModel tach;
    tach.run(3000, 100);
    const uint64_t last_edge = tach.now;
    REQUIRE(tach.rpm() == 3000);

    // The last value stays until the tracker gives up
    tach.advance_to(last_edge + stall_ticks - 0x1'0000);
    CHECK(tach.rpm() == 3000);
    tach.advance_to(last_edge + stall_ticks);
    CHECK(tach.rpm() == 0);

    SECTION("Restart: the first edge after a stall is not a period, the second is") {
        tach.advance_to(tach.now + 5 * stall_ticks);
        tach.edge_at(tach.now + 1);
        CHECK(tach.rpm() == 0);
        tach.edge_at(tach.now + 10'000);
        CHECK(tach.rpm() == 3000);
    }
}

TEST_CASE("xbe tach: a fan below the measurable speed reads 0", "[xbe][tach]") {
    // A period that spans 16 timer overflows (65,536 µs each) is invalid.
    // Below 28.6 RPM, every period spans 16. Above 30.5 RPM, no period spans
    // 16. Between the two, it depends on the phase of the edges.
    TachModel tach;
    tach.run(28, 10);
    CHECK(tach.rpm() == 0);
    tach.run(31, 10);
    CHECK(tach.rpm() == 31);
}

TEST_CASE("xbe w_led: PWM timer prescaler", "[xbe][pwm]") {
    CHECK(hal::math::w_led_prescaler_register(0) == hal::math::default_pwm_prescaler - 1);
    // The belt tuning strobe uses 64 to 134 Hz
    CHECK(hal::math::w_led_prescaler_register(100) == 2352 - 1);
    CHECK(hal::math::w_led_prescaler_register(4) == 58823 - 1);
    CHECK(hal::math::w_led_prescaler_register(65535) == 3 - 1);
}

TEST_CASE("xbe registers: RPM", "[xbe][registers]") {
    CHECK(rpm_register(0) == 0);
    CHECK(rpm_register(6700) == 6700);
    CHECK(rpm_register(65535) == 65535);
    CHECK(rpm_register(65536) == 65535);
    CHECK(rpm_register(131578) == 65535);
    CHECK(rpm_register(30'000'000) == 65535);
}

TEST_CASE("xbe tach: a noise edge does not give a plausible wrong RPM", "[xbe][tach]") {
    // The input filter of TIM1 rejects pulses shorter than approximately
    // 0.53 µs. A longer glitch on the tachometer wire is one more edge.
    TachModel tach;
    tach.run(3000, 100);
    REQUIRE(rpm_register(tach.rpm()) == 3000);

    // A glitch 228 µs after an edge: 131,578 RPM
    tach.edge_at(tach.now + 228);
    CHECK(tach.rpm() == 131578);
    // The register must not wrap to 506 RPM, a speed that a slow fan can have
    CHECK(rpm_register(tach.rpm()) == 65535);
}

TEST_CASE("xbe w_led: the prescaler fits the 16-bit PSC register", "[xbe][pwm]") {
    // TIM3->PSC has 16 bits. A larger value loses its upper bits, and the
    // frequency is then wrong by a large factor.
    for (uint32_t freq = 1; freq <= 65535; ++freq) {
        CAPTURE(freq);
        REQUIRE(hal::math::w_led_prescaler_register(static_cast<uint16_t>(freq)) <= 0xFFFF);
    }
    // The lowest frequency is approximately 3.6 Hz
    CHECK(hal::math::w_led_prescaler_register(1) == 0xFFFF);
    CHECK(hal::math::w_led_prescaler_register(3) == 0xFFFF);
}

namespace {

/// Like TachModel, but the IRQ handler runs \p latency ticks after the first
/// pending event. Thus an edge and an overflow near each other come in one
/// IRQ, in either order. The tracker then must guess the order from the
/// captured value.
/// \returns the number of edges after which the RPM was not \p expected
uint32_t wrong_rpm_with_irq_latency(uint32_t rpm, uint32_t expected, uint64_t first_edge, uint64_t latency, uint32_t edges) {
    uint32_t wrong = 0;
    TimerEventPeriodTracker tracker;
    const uint64_t period = 30'000'000 / rpm;
    uint64_t next_edge = first_edge;
    uint64_t next_overflow = 0x1'0000;
    uint32_t edges_done = 0;
    while (edges_done < edges) {
        const uint64_t irq_at = std::min(next_edge, next_overflow) + latency;
        bool capture = false;
        bool overflow = false;
        uint16_t ccr = 0;
        if (next_edge <= irq_at) {
            capture = true;
            ccr = static_cast<uint16_t>(next_edge & 0xFFFF);
            next_edge += period;
            ++edges_done;
            REQUIRE(next_edge > irq_at); // One capture for each IRQ, no overcapture
        }
        if (next_overflow <= irq_at) {
            overflow = true;
            next_overflow += 0x1'0000;
        }
        tracker.handle_multi_event(ccr, capture, overflow);
        if (capture && edges_done >= 2 && hal::math::tach_period_to_rpm(tracker.period_unsafe()) != expected) {
            ++wrong;
        }
    }
    return wrong;
}

} // namespace

TEST_CASE("xbe tach: an edge and an overflow in one IRQ", "[xbe][tach]") {
    // The tracker decides the order by the captured value (below 0x800: the
    // overflow came first). That is correct for an IRQ latency below 2048 µs.
    // Below 458 RPM, a period is longer than one timer cycle.
    const uint64_t latency = GENERATE(1u, 20u, 200u, 2000u);
    const uint32_t rpm = GENERATE(100u, 300u, 457u, 1000u, 3000u, 6700u);
    CAPTURE(latency, rpm);
    const uint64_t period = 30'000'000 / rpm;
    if (latency >= period) {
        SKIP("The IRQ must come before the next edge");
    }
    const uint32_t expected = 30'000'000 / period;
    // Put the edges just before and just after many overflows
    for (uint64_t first_edge = 0x1'0000 - 2 * latency - 3; first_edge < 0x1'0000 + 2 * latency + 3; first_edge += std::max<uint64_t>(1, latency / 8)) {
        CAPTURE(first_edge);
        REQUIRE(wrong_rpm_with_irq_latency(rpm, expected, first_edge, latency, 200) == 0);
    }
}
