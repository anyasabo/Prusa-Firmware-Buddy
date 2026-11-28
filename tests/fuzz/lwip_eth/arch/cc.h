// Host port of lwIP's arch/cc.h for the lwip_eth_input fuzz harness.
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define LWIP_ERRNO_STDINCLUDE 1
#define LWIP_TIMEVAL_PRIVATE  0
#include <sys/time.h>

#define LWIP_PLATFORM_DIAG(x) \
    do {                      \
        printf x;             \
    } while (0)
// On the printer, an lwIP assert is a BSOD.
#define LWIP_PLATFORM_ASSERT(x)                                                  \
    do {                                                                         \
        fprintf(stderr, "lwIP assert \"%s\" at %s:%d\n", x, __FILE__, __LINE__); \
        abort();                                                                 \
    } while (0)

#ifdef __cplusplus
extern "C" {
#endif
// Deterministic for each input (the harness resets the seed).
uint32_t fuzz_lwip_rand(void);
#ifdef __cplusplus
}
#endif
#define LWIP_RAND() fuzz_lwip_rand()

// The macOS SDK already defines htons() and friends as macros.
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS 1
