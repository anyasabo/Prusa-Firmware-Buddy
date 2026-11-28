#pragma once
// Configuration of TF-PSA-Crypto (TF_PSA_CRYPTO_CONFIG_FILE): the cryptography part of mbedtls 4.
// The TLS and X.509 part is in cipher_config_ece.h.
//
// The PSA_WANT_xxx macros select the mechanisms. The library adds the internal modules that they
// need.

#include <option/board_is_master_board.h>
#include <option/has_e2ee_support.h>

#define TF_PSA_CRYPTO_CONFIG_VERSION 0x01000000

/* Mechanisms */

// The puppies use only SHA-256 (the self hash of their flash). Without this condition, the PSA hash
// dispatch links SHA-1 and MD5 into them too (approximately 8 KB).
#define PSA_WANT_ALG_SHA_256 1
// The random generator (CTR_DRBG) needs AES.
#define PSA_WANT_KEY_TYPE_AES 1

#if BOARD_IS_MASTER_BOARD()
    // TLS 1.2 with TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 on P-256.
    #define PSA_WANT_ALG_ECDH                       1
    #define PSA_WANT_ALG_ECDSA                      1
    #define PSA_WANT_ALG_GCM                        1
    #define PSA_WANT_ALG_HMAC                       1
    #define PSA_WANT_ALG_TLS12_PRF                  1
    #define PSA_WANT_ECC_SECP_R1_256                1
    #define PSA_WANT_KEY_TYPE_DERIVE                1
    #define PSA_WANT_KEY_TYPE_HMAC                  1
    #define PSA_WANT_KEY_TYPE_ECC_PUBLIC_KEY        1
    #define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_BASIC    1
    #define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_IMPORT   1
    #define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_EXPORT   1
    #define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_GENERATE 1

    // SHA-1 (WebSocket handshake, OpenPrintTag UUIDs), MD5 (HTTP digest authentication of
    // PrusaLink).
    #define PSA_WANT_ALG_SHA_1 1
    #define PSA_WANT_ALG_MD5   1

    // Symmetric encryption of Connect transfers (iX).
    #define PSA_WANT_ALG_CTR 1
#endif

#if HAS_E2EE_SUPPORT()
    // End-to-end encrypted G-code: RSA-2048 printer key, RSA-PSS signatures,
    // RSA-OAEP key blocks, AES-CBC (on top of ECB) and HMAC-SHA256 of the G-code blocks.
    #define PSA_WANT_ALG_ECB_NO_PADDING             1
    #define PSA_WANT_ALG_RSA_OAEP                   1
    #define PSA_WANT_ALG_RSA_PSS                    1
    #define PSA_WANT_KEY_TYPE_RSA_PUBLIC_KEY        1
    #define PSA_WANT_KEY_TYPE_RSA_KEY_PAIR_BASIC    1
    #define PSA_WANT_KEY_TYPE_RSA_KEY_PAIR_IMPORT   1
    #define PSA_WANT_KEY_TYPE_RSA_KEY_PAIR_EXPORT   1
    #define PSA_WANT_KEY_TYPE_RSA_KEY_PAIR_GENERATE 1
    #define MBEDTLS_PK_WRITE_C
    #define MBEDTLS_MPI_MAX_SIZE    512
    #define MBEDTLS_MPI_WINDOW_SIZE 1
#endif

/* PSA core */

#define MBEDTLS_PSA_CRYPTO_C
// The firmware does not give PSA buffers that another party can change during the call. Without
// this option, PSA copies each input and output to the heap. For example, the puppy self hash
// copies its whole flash image.
#define MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS
// A fixed key store in static RAM. The dynamic store keeps approximately 2.8 KB of heap after the
// first TLS connection (a cache for persistent keys, which the firmware does not have, and the
// first slice of 16 slots). A TLS 1.2 handshake has at most approximately 5 keys at a time (ECDHE,
// the certificate check, the traffic keys). The key material stays on the heap.
#define MBEDTLS_PSA_KEY_SLOT_COUNT 8

// The random generator is CTR_DRBG, seeded from the hardware RNG.
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_PSA_DRIVER_GET_ENTROPY
// The entropy accumulator uses SHA-256 and not SHA-512, which the firmware does not build.
#define MBEDTLS_PSA_CRYPTO_RNG_HASH PSA_ALG_SHA_256

/* Formats and other modules */

#if BOARD_IS_MASTER_BOARD()
    #define MBEDTLS_ASN1_PARSE_C
    #define MBEDTLS_ASN1_WRITE_C
    #define MBEDTLS_BASE64_C
    #define MBEDTLS_MD_C
    #define MBEDTLS_PK_C
    #define MBEDTLS_PK_PARSE_C
#endif

/* Implementation choices */

// p256-m: ECDH, ECDSA and key generation on P-256 in less than 3 KB of code
// (its README). It replaces the generic bignum and ECP code.
#if BOARD_IS_MASTER_BOARD()
    #define MBEDTLS_PSA_P256M_DRIVER_ENABLED
#endif
#define MBEDTLS_HAVE_ASM
// Prefer smaller code over fast computations (our CPU is idle most of the time anyway).
#define MBEDTLS_AES_ROM_TABLES
#define MBEDTLS_AES_FEWER_TABLES
#define MBEDTLS_SHA256_SMALLER

/* Platform */

#define MBEDTLS_HAVE_TIME
// Needed for certificate expiration checks.
#define MBEDTLS_HAVE_TIME_DATE
// The firmware gives mbedtls_platform_gmtime_r() (newlib gmtime() allocates) and mbedtls_ms_time()
// (newlib has no clock_gettime()).
#define MBEDTLS_PLATFORM_GMTIME_R_ALT
#define MBEDTLS_PLATFORM_MS_TIME_ALT

/*
 * Our "primary" calloc BSODs on out of memory. We want to provide an
 * alternative one which returns nullptr instead.
 */
#include <stdlib.h> // For size_t
#ifdef __cplusplus
extern "C" {
#endif
void *calloc_fallible(size_t nmeb, size_t size); // Defined in heap.cpp
#ifdef __cplusplus
}
#endif
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_CALLOC_MACRO calloc_fallible /**< Default allocator macro to use, can be undefined */
#define MBEDTLS_PLATFORM_FREE_MACRO   free /**< Default free macro to use, can be undefined */
