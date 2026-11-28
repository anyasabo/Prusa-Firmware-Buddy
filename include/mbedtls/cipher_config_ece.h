#pragma once
// Configuration of the TLS and X.509 part of mbedtls 4 (MBEDTLS_CONFIG_FILE).
// The cryptography is configured in crypto_config_ece.h (TF_PSA_CRYPTO_CONFIG_FILE).

#include <option/board_is_master_board.h>

#define MBEDTLS_CONFIG_VERSION 0x04000000

// The puppies link mbedtls only for SHA-256.
#if BOARD_IS_MASTER_BOARD()

    #define MBEDTLS_SSL_TLS_C
    #define MBEDTLS_SSL_CLI_C
    #define MBEDTLS_SSL_PROTO_TLS1_2
    #define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
    #define MBEDTLS_SSL_CIPHERSUITES MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256

    /* for enabling SNI extension*/
    #define MBEDTLS_SSL_SERVER_NAME_INDICATION

    #define MBEDTLS_X509_CRT_PARSE_C
    #define MBEDTLS_X509_USE_C
    // The firmware does not print certificates. This removes the OID descriptions from flash.
    #define MBEDTLS_X509_REMOVE_INFO

    #define MBEDTLS_SSL_OUT_CONTENT_LEN 512
    #define MBEDTLS_SSL_IN_CONTENT_LEN  1024
    // Tell the server we don't want big fragments (relates to MBEDTLS_SSL_OUT_CONTENT_LEN)
    #define MBEDTLS_SSL_MAX_FRAGMENT_LENGTH
#endif
