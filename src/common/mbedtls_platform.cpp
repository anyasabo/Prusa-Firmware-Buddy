// Platform callbacks of mbedtls (TF-PSA-Crypto): the entropy source and the clocks.
#include <algorithm>
#include <string.h>
#include <time.h>
#include <mbedtls/platform.h>
#include <mbedtls/platform_util.h>
#include <random/random.h>
#include <timing.h>

// The entropy source of the PSA random generator (MBEDTLS_PSA_DRIVER_GET_ENTROPY). The random
// generator is a CTR_DRBG. It calls this to seed and reseed.
extern "C" int mbedtls_platform_get_entropy(psa_driver_get_entropy_flags_t flags, size_t *estimate_bits, unsigned char *output, size_t output_size) {
    // No flag is defined yet. The interface requires this answer for unknown flags.
    if (flags != 0) {
        return PSA_ERROR_NOT_SUPPORTED;
    }

    size_t bytes_written = 0;
    while (bytes_written < output_size) {
        uint32_t random_value;
        if (!rand_u_secure(&random_value)) {
            return PSA_ERROR_INSUFFICIENT_ENTROPY;
        }
        const size_t to_write = std::min(output_size - bytes_written, sizeof random_value);
        memcpy(output + bytes_written, &random_value, to_write);
        bytes_written += to_write;
    }

    // The hardware RNG gives full entropy. TF-PSA-Crypto 1.1 requires this value.
    *estimate_bits = 8 * output_size;
    return 0;
}

// MBEDTLS_PLATFORM_GMTIME_R_ALT: the default implementation uses gmtime(), which allocates on
// newlib.
extern "C" struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *tt, struct tm *tm_buf) {
    return gmtime_r(tt, tm_buf);
}

// MBEDTLS_PLATFORM_MS_TIME_ALT: newlib has no clock_gettime(). mbedtls uses this clock only for
// time differences (TLS 1.3 ticket age). Seconds since boot do not wrap for 136 years.
extern "C" mbedtls_ms_time_t mbedtls_ms_time(void) {
    return static_cast<mbedtls_ms_time_t>(ticks_s()) * 1000;
}
