// EXPERIMENT (branch upload-switches): the file name syntax of the upload switches.
#include <upload_switches.hpp>

#include <catch2/catch_test_macros.hpp>

using upload_switches::parse;
using upload_switches::Values;

TEST_CASE("upload_switches: a name without upx- changes nothing", "[upload_switches]") {
    Values v { .rx_desc = 6, .ooseq_fix = 0, .queue_cap = 8 };
    CHECK_FALSE(parse("/usb/test.gcode", v));
    CHECK_FALSE(parse(nullptr, v));
    CHECK(v.rx_desc == 6);
    CHECK(v.ooseq_fix == 0);
    CHECK(v.queue_cap == 8);
}

TEST_CASE("upload_switches: boot values are the fixes", "[upload_switches]") {
    const Values v {};
    CHECK(v.rx_desc == 10);
    CHECK(v.ooseq_fix == 1);
    CHECK(v.queue_cap == 17);
}

TEST_CASE("upload_switches: stock and the fixes", "[upload_switches]") {
    Values v;
    CHECK(parse("/usb/upx-r4-a0-q8-test.gcode", v));
    CHECK(v.rx_desc == 4);
    CHECK(v.ooseq_fix == 0);
    CHECK(v.queue_cap == 8);

    CHECK(parse("/usb/upx-r10-a1-q17-test.gcode", v));
    CHECK(v.rx_desc == 10);
    CHECK(v.ooseq_fix == 1);
    CHECK(v.queue_cap == 17);
}

TEST_CASE("upload_switches: keys not given keep their value", "[upload_switches]") {
    Values v { .rx_desc = 6, .ooseq_fix = 0, .queue_cap = 8 };
    CHECK(parse("/usb/upx-r8-mac.gcode", v));
    CHECK(v.rx_desc == 8);
    CHECK(v.ooseq_fix == 0);
    CHECK(v.queue_cap == 8);
}

TEST_CASE("upload_switches: values out of range are clamped", "[upload_switches]") {
    Values v;
    CHECK(parse("/usb/upx-r1-q0-x.gcode", v));
    CHECK(v.rx_desc == 2);
    CHECK(v.queue_cap == 1);
    CHECK(parse("/usb/upx-r99-q99-x.gcode", v));
    CHECK(v.rx_desc == 10);
    CHECK(v.queue_cap == 17);
    CHECK(parse("/usb/upx-a7-x.gcode", v));
    CHECK(v.ooseq_fix == 1);
}

TEST_CASE("upload_switches: parsing stops at an unknown key or a key without a number", "[upload_switches]") {
    Values v;
    CHECK(parse("/usb/upx-r6-w1-q8.gcode", v));
    CHECK(v.rx_desc == 6);
    CHECK(v.queue_cap == 17); // after the unknown key w

    Values w;
    CHECK(parse("/usb/upx-ring-q8.gcode", w)); // "ring": r without a number
    CHECK(w.rx_desc == 10);
    CHECK(w.queue_cap == 17);

    Values x;
    CHECK(parse("/usb/upx-r6q8.gcode", x)); // no '-' after the value: stop
    CHECK(x.rx_desc == 6);
    CHECK(x.queue_cap == 17);

    Values y;
    CHECK(parse("/usb/upx-r-4-q8.gcode", y)); // a sign: stop
    CHECK(y.rx_desc == 10);
    CHECK(y.queue_cap == 17);

    Values z;
    CHECK(parse("/usb/upx-r 4.gcode", z)); // a space: stop
    CHECK(z.rx_desc == 10);
}
