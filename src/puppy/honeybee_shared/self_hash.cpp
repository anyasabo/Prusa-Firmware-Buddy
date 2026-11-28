
#include "self_hash.hpp"

#include <device/hal.h>
#include <device/peripherals.hpp>

#ifndef HASH_ALGOSELECTION_SHA256
    #include <psa/crypto.h>
#endif /* HASH_ALGOSELECTION_SHA256 */

// Linker script constants
extern const uint32_t __fw_descriptor_start;
extern const uint32_t __fw_descriptor_length;
extern const uint32_t __flash_start;

bool self_hash_get(uint32_t salt, uint8_t output[32]) {
    bool ret = false;

    // Get the application area including the descriptor
    const uint8_t *APP_START = reinterpret_cast<const uint8_t *>(&__flash_start);
    const size_t APP_SIZE = reinterpret_cast<const uint8_t *>(&__fw_descriptor_start)
        - reinterpret_cast<const uint8_t *>(&__flash_start)
        + reinterpret_cast<size_t>(&__fw_descriptor_length);

#ifdef HASH_ALGOSELECTION_SHA256
    // #error dead code found by automatic analyses (see BFW-5461)

    // Use hardware sha when available
    ret = (HAL_HASH_Accumulate(&hhash, reinterpret_cast<uint8_t *>(&salt), sizeof(salt), HAL_MAX_DELAY) == HAL_OK);
    ret &= (HAL_HASH_AccumulateLast(&hhash, APP_START, APP_SIZE, output, HAL_MAX_DELAY) == HAL_OK);

#else /* HASH_ALGOSELECTION_SHA256 */

    psa_hash_operation_t ctx = PSA_HASH_OPERATION_INIT;
    size_t hash_len;
    ret = psa_hash_setup(&ctx, PSA_ALG_SHA_256) == PSA_SUCCESS
        && psa_hash_update(&ctx, reinterpret_cast<uint8_t *>(&salt), sizeof(salt)) == PSA_SUCCESS
        && psa_hash_update(&ctx, APP_START, APP_SIZE) == PSA_SUCCESS
        && psa_hash_finish(&ctx, output, 32, &hash_len) == PSA_SUCCESS;
    psa_hash_abort(&ctx);

#endif /* HASH_ALGOSELECTION_SHA256 */

    return ret;
}
