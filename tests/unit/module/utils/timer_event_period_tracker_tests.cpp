#include <catch2/catch_test_macros.hpp>
#include <utils/timing/timer_event_period_tracker.hpp>

TEST_CASE("timer_event_period_tracker", "[timer_event_period_tracker]") {
    TimerEventPeriodTracker t;

    // No data yet - we should get an invalid result
    REQUIRE(t.period_unsafe() == TimerEventPeriodTracker::invalid_period);

    // Overflow shouldn't do anything
    t.handle_timer_overflow();
    REQUIRE(t.period_unsafe() == TimerEventPeriodTracker::invalid_period);

    // Still no data
    t.handle_event(10);
    REQUIRE(t.period_unsafe() == TimerEventPeriodTracker::invalid_period);

    // Now we should get first data
    t.handle_event(20);
    REQUIRE(t.period_unsafe() == 10);

    t.handle_event(21);
    REQUIRE(t.period_unsafe() == 1);

    t.handle_timer_overflow();
    t.handle_event(1);
    REQUIRE(t.period_unsafe() == 65536 + 1 - 21);

    t.handle_timer_overflow();
    t.handle_event(1);
    REQUIRE(t.period_unsafe() == 65536);

    t.handle_timer_overflow();
    t.handle_event(12500);
    REQUIRE(t.period_unsafe() == 65536 + 12500 - 1);

    t.handle_timer_overflow();
    t.handle_timer_overflow();
    t.handle_event(1);
    REQUIRE(t.period_unsafe() == 65536 * 2 + 1 - 12500);

    for (uint32_t i = 1; i < TimerEventPeriodTracker::max_timer_overflows; i++) {
        INFO(i);

        t.handle_event(1);

        for (uint32_t j = 0; j < i; j++) {
            t.handle_timer_overflow();
        }

        t.handle_event(1);
        REQUIRE(t.period_unsafe() == 65536 * i);
    }

    {
        t.handle_event(1);

        for (uint32_t i = 0; i < TimerEventPeriodTracker::max_timer_overflows; i++) {
            t.handle_timer_overflow();
        }

        UNSCOPED_INFO("The last event was too long ago -> invalid period_unsafe");
        REQUIRE(t.period_unsafe() == TimerEventPeriodTracker::invalid_period);

        t.handle_event(1);
        UNSCOPED_INFO("The distance between two events was too much -> still invalid period_unsafe");
        REQUIRE(t.period_unsafe() == TimerEventPeriodTracker::invalid_period);

        t.handle_event(2);
        REQUIRE(t.period_unsafe() == 1);
    }
}

TEST_CASE("timer_event_period_tracker::multi_event", "[timer_event_period_tracker]") {
    TimerEventPeriodTracker t;

    // Overflow & event came in the same time - since the value is 12, the overflow probably happened before
    t.handle_multi_event(12, true, true);

    // Not enough data yet
    REQUIRE(t.period_unsafe() == TimerEventPeriodTracker::invalid_period);

    // Overflow again at the same time - we should now be exactly one period apart
    t.handle_multi_event(12, true, true);
    REQUIRE(t.period_unsafe() == 65536);

    // Overflow again, this time it likely happened after the capture
    t.handle_multi_event(40000, true, true);
    REQUIRE(t.period_unsafe() == 40000 - 12);

    t.handle_event(41000);

    // Overflow again, this time it still likely happened after the capture,
    // but in that case we would have negative time, so it must have been after
    t.handle_multi_event(39000, true, true);
    REQUIRE(t.period_unsafe() == 65536 + 39000 - 41000);

    // Overflow, very likely to have happened before capture
    t.handle_multi_event(1, true, true);
    REQUIRE(t.period_unsafe() == 1 + 65536 - 39000);
}

TEST_CASE("timer_event_period_tracker::first_capture_then_overflow_in_one_irq", "[timer_event_period_tracker]") {
    // After a long time with no edge, the first edge and the next overflow come
    // in one IRQ. The edge came first (the timer value is high), thus the
    // overflow counts toward the next period.
    TimerEventPeriodTracker t;
    t.handle_multi_event(0xFFFF, true, true);
    REQUIRE(t.period_unsafe() == TimerEventPeriodTracker::invalid_period);

    // The next edge, 300,000 ticks later (a fan at 100 RPM)
    for (int i = 0; i < 4; i++) {
        t.handle_timer_overflow();
    }
    t.handle_event((0xFFFF + 300'000) & 0xFFFF);
    CHECK(t.period_unsafe() == 300'000);
}

TEST_CASE("timer_event_period_tracker::overflow_then_capture_in_one_irq_after_a_long_time", "[timer_event_period_tracker]") {
    // The overflow came first (the timer value is low). The previous edge was
    // too long ago, thus the period stays invalid.
    TimerEventPeriodTracker t;
    t.handle_event(5);
    for (uint32_t i = 0; i < TimerEventPeriodTracker::max_timer_overflows; i++) {
        t.handle_timer_overflow();
    }
    t.handle_multi_event(3, true, true);
    CHECK(t.period_unsafe() == TimerEventPeriodTracker::invalid_period);
    t.handle_event(1003);
    CHECK(t.period_unsafe() == 1000);
}
