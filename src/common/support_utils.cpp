#include "support_utils.h"

#include "otp.hpp"
#include <psa/crypto.h>
#include "support_utils_lib.hpp"
#include <bsod/bsod.h>

void printerHash(char *str, size_t size) {
    serial_nr_t serial_nr;
    const uint8_t serial_nr_len = otp_get_serial_nr(serial_nr);

    uint8_t hash[32];
    psa_hash_operation_t ctx = PSA_HASH_OPERATION_INIT;
    psa_hash_setup(&ctx, PSA_ALG_SHA_256);

    /// CPU ID
    psa_hash_update(&ctx, otp_get_STM32_UUID()->uuid, sizeof(otp_get_STM32_UUID()->uuid));

    /// MAC
    psa_hash_update(&ctx, otp_get_mac_address()->mac, sizeof(otp_get_mac_address()->mac));

    /// SN
    psa_hash_update(&ctx, reinterpret_cast<const unsigned char *>(serial_nr.begin()), serial_nr_len);

    size_t hash_len;
    if (psa_hash_finish(&ctx, hash, sizeof hash, &hash_len) != PSA_SUCCESS) {
        // The printer hash identifies the printer. A wrong value must not leave the printer.
        bsod("printerHash");
    }

    /// convert number by 5-bit chunks (32 symbol alphabet)
    debug_assert(sizeof(hash) * 8 >= size * 5);
    for (uint8_t i = 0; i < size; ++i) {
        str[i] = to32(hash, i * 5);
    }
}
