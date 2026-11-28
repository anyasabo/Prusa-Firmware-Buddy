#include "sha256_multiuse.hpp"
#include <bsod/bsod.h>

namespace e2ee {

SHA256MultiuseHash::SHA256MultiuseHash() {
    reset();
}

SHA256MultiuseHash::~SHA256MultiuseHash() {
    psa_hash_abort(&sha256_ctx);
}

bool SHA256MultiuseHash::get_hash(uint8_t *buffer, [[maybe_unused]] size_t buffer_size) {
    debug_assert(buffer_size == e2ee::HASH_SIZE);
    size_t hash_len;
    const bool ok = psa_hash_finish(&sha256_ctx, buffer, e2ee::HASH_SIZE, &hash_len) == PSA_SUCCESS;
    reset();
    return ok;
}

void SHA256MultiuseHash::update(const uint8_t *data, size_t size) {
    psa_hash_update(&sha256_ctx, data, size);
}

void SHA256MultiuseHash::reset() {
    psa_hash_abort(&sha256_ctx);
    psa_hash_setup(&sha256_ctx, PSA_ALG_SHA_256);
}

} // namespace e2ee
