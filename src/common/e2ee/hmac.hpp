#pragma once

#include "e2ee.hpp"
#include <psa/crypto.h>
#include <core/core.hpp>

namespace e2ee {

class HMAC {
public:
    HMAC(const uint8_t *sign_key, size_t key_len);
    void update(uint8_t *data, size_t size);
    /// Returns false if the HMAC could not be computed (for example, out of memory in the constructor).
    [[nodiscard]] bool finish(uint8_t *output, [[maybe_unused]] size_t size);
    ~HMAC();

private:
    psa_mac_operation_t operation = PSA_MAC_OPERATION_INIT;
};

template <typename CB>
void block_header_bytes_cb(bgcode::core::BlockHeader header, CB callback) {
    callback(reinterpret_cast<uint8_t *>(&header.type), sizeof(header.type));
    callback(reinterpret_cast<uint8_t *>(&header.compression), sizeof(header.compression));
    callback(reinterpret_cast<uint8_t *>(&header.uncompressed_size), sizeof(header.uncompressed_size));
    if ((bgcode::core::ECompressionType)header.compression != bgcode::core::ECompressionType::None) {
        callback(reinterpret_cast<uint8_t *>(&header.compressed_size), sizeof(header.compressed_size));
    }
}

enum class CheckResult {
    // general error
    ERROR,
    OK,
    // Check failed
    CORRUPTED

};

CheckResult check_hmac_and_crc(FILE *file, bgcode::core::BlockHeader header, e2ee::SymmetricCipherInfo &info, bool check_crc);

} // namespace e2ee
