#include <core/core.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

using bgcode::core::detail::checked_end_position;

// On the printer, long is 32 bits. A test on a 64-bit host checks that case with int32_t.
TEST_CASE("bgcode: the end position of a block does not wrap in a 32-bit offset", "[bgcode]") {
    // A FileMetadata block without compression, in a file with CRC32: header 8 B, parameters 2 B,
    // checksum 4 B. With uncompressed_size 0xFFFFFFF2, the content is 0xFFFFFFF8 B. In 32 bits, the
    // old sum was position + 8 - 8: the block itself, and the block iteration never ended.
    const uint64_t size = 8 + uint64_t(2) + 0xFFFFFFF2u + 4;
    CHECK(checked_end_position<int32_t>(10, size) == -1);

    // uncompressed_size 0xFFFFFFFF: the content size wrapped to 5 in a 32-bit size_t
    CHECK(checked_end_position<int32_t>(10, 8 + uint64_t(2) + 0xFFFFFFFFu + 4) == -1);
}

TEST_CASE("bgcode: the end position of a block at the limits of the offset type", "[bgcode]") {
    constexpr int32_t max = std::numeric_limits<int32_t>::max();
    CHECK(checked_end_position<int32_t>(10, 100) == 110);
    CHECK(checked_end_position<int32_t>(max - 5, 5) == max);
    CHECK(checked_end_position<int32_t>(max - 5, 6) == -1);
    CHECK(checked_end_position<int32_t>(-1, 1) == -1);
    CHECK(checked_end_position<int64_t>(10, 8 + uint64_t(2) + 0xFFFFFFF2u + 4) == 10 + 0x100000000LL);
}
