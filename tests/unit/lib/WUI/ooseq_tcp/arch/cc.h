// Minimal host port of lwIP's arch/cc.h for the ooseq_tcp unit test.
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
#define LWIP_PLATFORM_ASSERT(x)                                                  \
    do {                                                                         \
        fprintf(stderr, "lwIP assert \"%s\" at %s:%d\n", x, __FILE__, __LINE__); \
        abort();                                                                 \
    } while (0)

#define LWIP_RAND() ((uint32_t)rand())

// The macOS SDK already defines htons() and friends as macros.
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS 1
