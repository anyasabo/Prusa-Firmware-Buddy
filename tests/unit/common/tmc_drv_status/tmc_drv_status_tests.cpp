#include <catch2/catch_test_macros.hpp>
#include "tmc_drv_status.hpp"

using buddy::decode_drv_status;

TEST_CASE("DRV_STATUS decode: each field at its TMC2130 bit position", "[tmc_drv_status]") {
    const auto zero = decode_drv_status(0);
    CHECK(zero.sg_result == 0);
    CHECK(zero.cs_actual == 0);
    CHECK_FALSE(zero.short_to_ground);
    CHECK_FALSE(zero.overtemp_warning);
    CHECK_FALSE(zero.open_load_a);
    CHECK_FALSE(zero.open_load_b);
    CHECK_FALSE(zero.standstill);

    CHECK(decode_drv_status(0x3FF).sg_result == 1023);
    CHECK(decode_drv_status(0x400).sg_result == 0); // bit 10 is not SG_RESULT
    CHECK(decode_drv_status(0x1F0000).cs_actual == 31);
    CHECK(decode_drv_status(1u << 26).overtemp_warning);
    CHECK(decode_drv_status(1u << 27).short_to_ground);
    CHECK(decode_drv_status(1u << 28).short_to_ground);
    CHECK(decode_drv_status(1u << 29).open_load_a);
    CHECK_FALSE(decode_drv_status(1u << 29).open_load_b);
    CHECK(decode_drv_status(1u << 30).open_load_b);
    CHECK(decode_drv_status(1u << 31).standstill);
}

TEST_CASE("DRV_STATUS decode: a running motor with an open coil", "[tmc_drv_status]") {
    // sg 300, cs 16, open load on coil B, not at standstill
    const auto s = decode_drv_status(300u | (16u << 16) | (1u << 30));
    CHECK(s.sg_result == 300);
    CHECK(s.cs_actual == 16);
    CHECK(s.open_load_b);
    CHECK_FALSE(s.open_load_a);
    CHECK_FALSE(s.standstill);
}
