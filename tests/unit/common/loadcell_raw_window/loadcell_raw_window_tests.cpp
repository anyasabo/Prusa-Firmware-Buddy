#include <catch2/catch_test_macros.hpp>
#include <loadcell_raw_window.hpp>

using buddy::LoadcellRawWindow;

TEST_CASE("loadcell_raw_window: no window gives no samples", "[loadcell_raw_window]") {
    LoadcellRawWindow window;
    window.feed(100);
    const auto r = window.stop();
    CHECK(r.samples == 0);
    CHECK(r.min == 0);
    CHECK(r.max == 0);
}

TEST_CASE("loadcell_raw_window: an empty window gives no samples", "[loadcell_raw_window]") {
    LoadcellRawWindow window;
    window.start();
    const auto r = window.stop();
    CHECK(r.samples == 0);
    CHECK(r.min == 0);
    CHECK(r.max == 0);
}

TEST_CASE("loadcell_raw_window: spread of the samples in the window", "[loadcell_raw_window]") {
    LoadcellRawWindow window;
    window.feed(-5'000'000); // before the window: ignored
    window.start();
    CHECK(window.is_active());
    window.feed(-1'100'000);
    window.feed(-1'100'400);
    window.feed(-1'099'800);
    const auto r = window.stop();
    CHECK_FALSE(window.is_active());
    CHECK(r.samples == 3);
    CHECK(r.min == -1'100'400);
    CHECK(r.max == -1'099'800);
    CHECK(r.spread() == 600);

    window.feed(0); // after the window: ignored
    CHECK(window.stop().samples == 0);
}

TEST_CASE("loadcell_raw_window: one sample has no spread", "[loadcell_raw_window]") {
    LoadcellRawWindow window;
    window.start();
    window.feed(42);
    const auto r = window.stop();
    CHECK(r.samples == 1);
    CHECK(r.min == 42);
    CHECK(r.max == 42);
    CHECK(r.spread() == 0);
}

TEST_CASE("loadcell_raw_window: start clears the previous window", "[loadcell_raw_window]") {
    LoadcellRawWindow window;
    window.start();
    window.feed(-1'000'000);
    window.feed(1'000'000);
    window.start();
    window.feed(10);
    window.feed(20);
    const auto r = window.stop();
    CHECK(r.samples == 2);
    CHECK(r.spread() == 10);
}

TEST_CASE("loadcell_raw_window: the HX717 rails do not overflow", "[loadcell_raw_window]") {
    LoadcellRawWindow window;
    window.start();
    window.feed(-0x80'0000);
    window.feed(0x7F'FFFF);
    CHECK(window.stop().spread() == 0xFF'FFFF);
}
