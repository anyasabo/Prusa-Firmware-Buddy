// e2ee::read_and_verify_identity_block(): the first E2EE structure the printer parses from an
// encrypted .bgcode, before it can verify anything. The file position is at the block content
// (sign-cypher u16, flags u8, then the identity payload), as in the unit test.
//
// Input: bytes 0-3 = declared block size (little-endian u32), byte 4 bit 0 = verify signature,
//        rest = block content written to the file.

#include <e2ee/e2ee.hpp>
#include <core/core.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 5) {
        return 0;
    }
    uint32_t block_size;
    memcpy(&block_size, data, sizeof(block_size));
    const bool verify = data[4] & 1;

    FILE *file = tmpfile();
    if (!file) {
        return 0;
    }
    fwrite(data + 5, 1, size - 5, file);
    fseek(file, 0, SEEK_SET);

    const bgcode::core::BlockHeader header(
        static_cast<uint16_t>(bgcode::core::EBlockType::IdentityBlock),
        static_cast<uint16_t>(bgcode::core::ECompressionType::None),
        block_size);
    e2ee::IdentityBlockInfo info;
    uint8_t intro_hash[e2ee::HASH_SIZE] = {};
    (void)e2ee::read_and_verify_identity_block(file, header, intro_hash, info, verify);
    fclose(file);
    return 0;
}
