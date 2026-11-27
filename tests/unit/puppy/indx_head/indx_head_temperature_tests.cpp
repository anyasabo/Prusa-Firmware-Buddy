#include <catch2/catch_test_macros.hpp>
#include <temperature_c100.hpp>

using indx_head::saturate_c100;

TEST_CASE("indx_head: temperatures above the int16_t range stay high", "[indx_head]") {
    CHECK(saturate_c100(25'000) == 25'000);
    CHECK(saturate_c100(32'767) == 32'767);
    // 328 °C is below the max temperature panic (330 °C). A cast to int16_t gives -32'736 (-327.36 °C).
    CHECK(saturate_c100(32'800) == 32'767);
    CHECK(saturate_c100(33'000) == 32'767);
    CHECK(saturate_c100(-40'000) == -32'768);
    // The app subtracts the compensation from the wide value and then saturates. 328 °C with a
    // compensation of 3 °C is 325 °C, in range. Saturated first, it would give 324.67 °C.
    CHECK(saturate_c100(32'800 - 300) == 32'500);
}
