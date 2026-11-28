#include "hmac.hpp"

#include <logging/log.hpp>
#include <bsod.h>
#include <crc32.h>

#include <cstring>

using bgcode::core::BlockHeader;
using bgcode::core::EBlockType;
using bgcode::core::ECompressionType;

LOG_COMPONENT_REF(PRUSA_PACK_READER);
namespace e2ee {

HMAC::HMAC(const uint8_t *sign_key, size_t key_len) {
    // The key is in the PSA key store only during the setup. The operation keeps its own copy.
    // If the setup fails, the operation stays inactive and finish() fails.
    if (psa_crypto_init() != PSA_SUCCESS) {
        return;
    }
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    mbedtls_svc_key_id_t key_id;
    if (psa_import_key(&attributes, sign_key, key_len, &key_id) != PSA_SUCCESS) {
        return;
    }
    psa_mac_sign_setup(&operation, key_id, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_destroy_key(key_id);
}

HMAC::~HMAC() {
    psa_mac_abort(&operation);
}

void HMAC::update(uint8_t *data, size_t size) {
    psa_mac_update(&operation, data, size);
}

bool HMAC::finish(uint8_t *output, [[maybe_unused]] size_t size) {
    debug_assert(size == e2ee::HMAC_SIZE);
    size_t mac_len;
    return psa_mac_sign_finish(&operation, output, e2ee::HMAC_SIZE, &mac_len) == PSA_SUCCESS;
}

CheckResult check_hmac_and_crc(FILE *file, BlockHeader header, e2ee::SymmetricCipherInfo &info, bool check_crc) {
    const bool check_hmac = (EBlockType)header.type == EBlockType::EncryptedBlock;
    long pos = ftell(file);
    HMAC hmac(info.keys->sign_key, std::size(info.keys->sign_key));
    uint32_t crc = 0;
    if (check_hmac) {
        block_header_bytes_cb(header, [&hmac](uint8_t *data, size_t size) {
            hmac.update(data, size);
        });
        uint8_t iv[16];
        auto header_pos = header.get_position();
        memset(iv, 0, sizeof(iv));
        memcpy(iv, reinterpret_cast<const uint8_t *>(&header_pos), sizeof header_pos);
        hmac.update(iv, sizeof(iv));
    }
    if (check_crc) {
        block_header_bytes_cb(header, [&crc](uint8_t *data, size_t size) {
            crc = crc32_calc_ex(crc, data, size);
        });
    }
    constexpr size_t BLOCK_SIZE = 64;
    uint8_t block[BLOCK_SIZE];
    const size_t hmacs_size = info.num_of_hmacs * e2ee::HMAC_SIZE;
    size_t hmac_data_size = bgcode::core::block_payload_size(header) - hmacs_size;
    while (hmac_data_size > 0) {
        size_t to_read = std::min(hmac_data_size, BLOCK_SIZE);
        if (fread(block, 1, to_read, file) != to_read) {
            return CheckResult::ERROR;
        }

        if (check_hmac) {
            hmac.update(block, to_read);
        }
        if (check_crc) {
            crc = crc32_calc_ex(crc, block, to_read);
        }
        hmac_data_size -= to_read;
    }
    // Read HMACs for crc
    if (check_crc) {
        size_t size = hmacs_size;
        while (size > 0) {
            size_t to_read = std::min(size, BLOCK_SIZE);
            if (fread(block, 1, to_read, file) != to_read) {
                return CheckResult::ERROR;
            }
            crc = crc32_calc_ex(crc, block, to_read);
            size -= to_read;
        }
        uint32_t read_crc;
        if (fread(&read_crc, sizeof(read_crc), 1, file) != 1) {
            return CheckResult::ERROR;
        }
        if (read_crc != crc) {
            return CheckResult::CORRUPTED;
        }
    }

    if (check_hmac) {
        uint8_t computed_hmac[e2ee::HMAC_SIZE];
        if (!hmac.finish(computed_hmac, sizeof(computed_hmac))) {
            return CheckResult::ERROR;
        }
        // read_block_header() checked that the block end fits in the file positions
        // (block_end_position()), so this sum does not wrap
        size_t hmac_pos = header.get_position() + header.get_size() + bgcode::core::block_payload_size(header) - info.num_of_hmacs * e2ee::HMAC_SIZE + info.hmac_index * e2ee::HMAC_SIZE;
        if (fseek(file, hmac_pos, SEEK_SET) != 0) {
            return CheckResult::ERROR;
        }
        uint8_t read_hmac[e2ee::HMAC_SIZE];
        if (fread(read_hmac, 1, sizeof(read_hmac), file) != sizeof(read_hmac)) {
            return CheckResult::ERROR;
        }
        if (memcmp(read_hmac, computed_hmac, e2ee::HMAC_SIZE) != 0) {
            log_info(PRUSA_PACK_READER, "HMAC mismatch in block starting at: %ld", header.get_position());
            return CheckResult::CORRUPTED;
        }
    }

    if (fseek(file, pos, SEEK_SET) != 0) {
        return CheckResult::ERROR;
    }
    return CheckResult::OK;
}
} // namespace e2ee
