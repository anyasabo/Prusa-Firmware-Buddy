#include "arc_segments.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>

TEST_CASE("arc_segment_count rounds the segment count") {
    CHECK(arc_segment_count(0.f, 0.1f) == 1);
    CHECK(arc_segment_count(0.01f, 0.1f) == 1);
    CHECK(arc_segment_count(1.f, 2.f) == 1);
    CHECK(arc_segment_count(10.f, 2.f) == 5);
    CHECK(arc_segment_count(10.5f, 2.f) == 6);
}

TEST_CASE("arc_segment_count gives more than 65535 segments") {
    // G2 I1100 J0 F300: full circle with radius 1.1 m and 0.1 mm segments
    CHECK(arc_segment_count(6911.5f, 0.1f) == 69115);
}

TEST_CASE("arc_segment_count limits the segment count") {
    // G2 I1e30 J0: the segment count is out of the range of uint32_t
    CHECK(arc_segment_count(1e30f, 2.f) == arc_segment_count_max);
    CHECK(arc_segment_count(std::numeric_limits<float>::infinity(), 2.f) == arc_segment_count_max);
}

TEST_CASE("arc_segment_count gives one segment for a value that is not a number") {
    CHECK(arc_segment_count(std::numeric_limits<float>::quiet_NaN(), 2.f) == 1);
    CHECK(arc_segment_count(1.f, std::numeric_limits<float>::quiet_NaN()) == 1);
}
