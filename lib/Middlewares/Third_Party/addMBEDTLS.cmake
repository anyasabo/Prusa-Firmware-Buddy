set(MBEDTLS_ROOT ${CMAKE_SOURCE_DIR}/lib/Middlewares/Third_Party/mbedtls)
set(TF_PSA_CRYPTO_ROOT ${MBEDTLS_ROOT}/tf-psa-crypto)

# The same source lists as the upstream CMake files (library/CMakeLists.txt and the CMakeLists.txt
# of each tf-psa-crypto directory). The configuration turns off what the firmware does not use, and
# --gc-sections removes the rest.
file(
  GLOB
  TF_PSA_CRYPTO_SOURCES
  ${TF_PSA_CRYPTO_ROOT}/core/*.c
  ${TF_PSA_CRYPTO_ROOT}/drivers/builtin/src/*.c
  ${TF_PSA_CRYPTO_ROOT}/extras/*.c
  ${TF_PSA_CRYPTO_ROOT}/platform/*.c
  ${TF_PSA_CRYPTO_ROOT}/utilities/*.c
  )

add_library(
  mbedTLS
  ${TF_PSA_CRYPTO_SOURCES}
  ${TF_PSA_CRYPTO_ROOT}/drivers/p256-m/p256-m_driver_entrypoints.c
  ${TF_PSA_CRYPTO_ROOT}/drivers/p256-m/p256-m/p256-m.c
  ${MBEDTLS_ROOT}/library/ssl_ciphersuites.c
  ${MBEDTLS_ROOT}/library/ssl_client.c
  ${MBEDTLS_ROOT}/library/ssl_msg.c
  ${MBEDTLS_ROOT}/library/ssl_tls.c
  ${MBEDTLS_ROOT}/library/ssl_tls12_client.c
  ${MBEDTLS_ROOT}/library/ssl_tls13_client.c
  ${MBEDTLS_ROOT}/library/ssl_tls13_generic.c
  ${MBEDTLS_ROOT}/library/ssl_tls13_keys.c
  ${MBEDTLS_ROOT}/library/x509.c
  ${MBEDTLS_ROOT}/library/x509_crt.c
  ${MBEDTLS_ROOT}/library/x509_oid.c
  )

target_include_directories(
  mbedTLS
  PUBLIC ${MBEDTLS_ROOT}/include ${TF_PSA_CRYPTO_ROOT}/include
         ${TF_PSA_CRYPTO_ROOT}/drivers/builtin/include ${TF_PSA_CRYPTO_ROOT}/drivers/p256-m
  PRIVATE ${MBEDTLS_ROOT}/library
          ${TF_PSA_CRYPTO_ROOT}/core
          ${TF_PSA_CRYPTO_ROOT}/dispatch
          ${TF_PSA_CRYPTO_ROOT}/extras
          ${TF_PSA_CRYPTO_ROOT}/platform
          ${TF_PSA_CRYPTO_ROOT}/utilities
          ${TF_PSA_CRYPTO_ROOT}/drivers/builtin/src
  )

target_compile_definitions(
  mbedTLS
  PUBLIC MBEDTLS_CONFIG_FILE=\"${CMAKE_SOURCE_DIR}/include/mbedtls/cipher_config_ece.h\"
         TF_PSA_CRYPTO_CONFIG_FILE=\"${CMAKE_SOURCE_DIR}/include/mbedtls/crypto_config_ece.h\"
  )

# The configuration includes <option/has_e2ee_support.h>.
target_include_directories(mbedTLS PUBLIC ${CMAKE_BINARY_DIR}/include)
