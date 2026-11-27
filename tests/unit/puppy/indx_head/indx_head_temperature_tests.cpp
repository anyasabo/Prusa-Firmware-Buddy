#include <catch2/catch_test_macros.hpp>
#include <temperature_c100.hpp>

using indx_head::saturate_c100;

TEST_CASE("indx_head: temperatures above the int16_t range stay high", "[indx_head]") {
    CHECK(saturate_c100(25'000) == 25'000);
    CHECK(saturate_c100(32'767) == 32'767);
    // 328 °C is below the max temperature panic (330 °C). A cast gave -32'736 (-327.36 °C).
    CHECK(saturate_c100(32'800) == 32'767);
    CHECK(saturate_c100(33'000) == 32'767);
    CHECK(saturate_c100(-40'000) == -32'768);
    // The compensation is subtracted after the conversion
    CHECK(saturate_c100(int32_t { saturate_c100(32'800) } - (-300)) == 32'767);
}
