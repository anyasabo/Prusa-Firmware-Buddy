// The platform callbacks of mbedtls for host unit tests. The firmware has them in
// src/common/mbedtls_platform.cpp.
#include <stdlib.h>
#include <time.h>
#include <mbedtls/platform.h>
#include <mbedtls/platform_util.h>

// Not secure: the unit tests need random numbers only for RSA blinding and ECC keys.
int mbedtls_platform_get_entropy(psa_driver_get_entropy_flags_t flags, size_t *estimate_bits, unsigned char *output, size_t output_size) {
    if (flags != 0) {
        return PSA_ERROR_NOT_SUPPORTED;
    }
    for (size_t i = 0; i < output_size; i++) {
        output[i] = (unsigned char)rand();
    }
    *estimate_bits = 8 * output_size;
    return 0;
}

struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *tt, struct tm *tm_buf) {
    return gmtime_r(tt, tm_buf);
}

mbedtls_ms_time_t mbedtls_ms_time(void) {
    return (mbedtls_ms_time_t)time(NULL) * 1000;
}
