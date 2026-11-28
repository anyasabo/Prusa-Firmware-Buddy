/// @file
#include <buddy/digest.hpp>

#include <bsod.h>
#include <logging/log.hpp>
#include <psa/crypto.h>
#include <timing.h>
#include <unistd.h>
#include <array>

LOG_COMPONENT_REF(Buddy);

bool buddy::compute_file_digest(const int fd, const uint32_t salt, Digest output) {
    if (lseek(fd, 0, SEEK_SET) == -1) {
        return false;
    }

    psa_hash_operation_t ctx = PSA_HASH_OPERATION_INIT;
    if (psa_hash_setup(&ctx, PSA_ALG_SHA_256) != PSA_SUCCESS
        || psa_hash_update(&ctx, reinterpret_cast<const uint8_t *>(&salt), sizeof(salt)) != PSA_SUCCESS) {
        psa_hash_abort(&ctx);
        return false;
    }

    std::array<uint8_t, 128> buffer;
    for (;;) {
        const ssize_t nread = read(fd, buffer.data(), buffer.size());
        if (nread == -1) {
            psa_hash_abort(&ctx);
            return false;
        } else if (nread == 0) {
            size_t hash_len;
            if (psa_hash_finish(&ctx, (unsigned char *)output.data(), output.size(), &hash_len) != PSA_SUCCESS) {
                psa_hash_abort(&ctx);
                return false;
            }
            return true;
        } else if (psa_hash_update(&ctx, buffer.data(), nread) != PSA_SUCCESS) {
            psa_hash_abort(&ctx);
            return false;
        }
    }
}

void buddy::compute_file_digest_with_retry(const int fd, const uint32_t salt, Digest output) {
    const auto started = ticks_ms();
    for (;;) {
        if (buddy::compute_file_digest(fd, salt, output)) {
            return;
        }
        if (ticks_diff(ticks_ms(), started) < 1'000) {
            log_error(Buddy, "compute_file_digest() failed, retrying");
        } else {
            bsod("compute_file_digest");
        }
    }
}
