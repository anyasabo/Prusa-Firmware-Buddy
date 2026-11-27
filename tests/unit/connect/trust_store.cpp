#include <tls/trust_store.hpp>

#include "trust_store_fixtures.h"

#include <catch2/catch_test_macros.hpp>
#include <mbedtls/x509_crt.h>
#include <option/has_tls_public_ca.h>

using namespace connect_client;

TEST_CASE("connect::trust_store prusa host", "[connect][tls]") {
    CHECK(is_prusa_host("prusa3d.com"));
    CHECK(is_prusa_host("buddy-a.connect.prusa3d.com"));
    CHECK(is_prusa_host("Buddy-A.Connect.PRUSA3D.com"));
    CHECK_FALSE(is_prusa_host("notprusa3d.com"));
    CHECK_FALSE(is_prusa_host("prusa3d.com.example.com"));
    CHECK_FALSE(is_prusa_host("connect.example.com"));
    CHECK_FALSE(is_prusa_host("3d.com"));
    CHECK_FALSE(is_prusa_host(""));
}

namespace {

class Verify {
    mbedtls_x509_crt anchors;
    mbedtls_x509_crt chain;

public:
    Verify(const char *host) {
        mbedtls_x509_crt_init(&anchors);
        mbedtls_x509_crt_init(&chain);
        for (const auto &der : trust_anchors(host)) {
            REQUIRE(mbedtls_x509_crt_parse_der_nocopy(&anchors, der.data(), der.size()) == 0);
        }
    }
    ~Verify() {
        mbedtls_x509_crt_free(&chain);
        mbedtls_x509_crt_free(&anchors);
    }
    Verify &add(std::span<const unsigned char> der) {
        REQUIRE(mbedtls_x509_crt_parse_der(&chain, der.data(), der.size()) == 0);
        return *this;
    }
    // The verification flags, without the validity period: the fixtures expire, the roots do not.
    uint32_t flags() {
        uint32_t flags = 0;
        mbedtls_x509_crt_verify(&chain, &anchors, nullptr, nullptr, &flags, nullptr, nullptr);
        return flags & ~(MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE);
    }
};

constexpr const char *prusa = "buddy-a.connect.prusa3d.com";
constexpr const char *self_hosted = "connect.example.com";

} // namespace

TEST_CASE("connect::trust_store Prusa Connect chain", "[connect][tls]") {
    CHECK(trust_anchors(prusa).size() == 1);
    CHECK(Verify(prusa).add(prusa_buddy_intermediate).flags() == 0);
    CHECK(Verify(self_hosted).add(prusa_buddy_intermediate).flags() == 0);
}

TEST_CASE("connect::trust_store a public CA cannot impersonate Prusa", "[connect][tls]") {
    CHECK(Verify(prusa).add(le_e7).flags() == MBEDTLS_X509_BADCERT_NOT_TRUSTED);
    CHECK(Verify(prusa).add(le_ye1).add(isrg_root_ye_by_x2).flags() == MBEDTLS_X509_BADCERT_NOT_TRUSTED);
}

#if HAS_TLS_PUBLIC_CA()
TEST_CASE("connect::trust_store Let's Encrypt chains", "[connect][tls]") {
    CHECK(trust_anchors(self_hosted).size() == 3);
    // E7 -> ISRG Root X2 (P-384, SHA-384).
    CHECK(Verify(self_hosted).add(le_e7).flags() == 0);
    // YE1 -> ISRG Root YE, and the chain that Caddy sends: YE1 -> Root YE (cross-signed) -> X2.
    CHECK(Verify(self_hosted).add(le_ye1).flags() == 0);
    CHECK(Verify(self_hosted).add(le_ye1).add(isrg_root_ye_by_x2).flags() == 0);
}
#endif

#if HAS_TLS_PUBLIC_CA()
    #include <mbedtls/ssl.h>

namespace {

// The input record length that MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH shrinks the buffer to after
// the handshake.
size_t input_length_after_handshake(unsigned char mfl_code) {
    mbedtls_ssl_config conf;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config_init(&conf);
    mbedtls_ssl_init(&ssl);
    REQUIRE(mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) == 0);
    if (mfl_code != MBEDTLS_SSL_MAX_FRAG_LEN_NONE) {
        REQUIRE(mbedtls_ssl_conf_max_frag_len(&conf, mfl_code) == 0);
    }
    REQUIRE(mbedtls_ssl_setup(&ssl, &conf) == 0);
    ssl.state = MBEDTLS_SSL_SERVER_HELLO_DONE;
    const size_t length = mbedtls_ssl_get_input_max_frag_len(&ssl);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    return length;
}

} // namespace

TEST_CASE("connect::tls input records after the handshake", "[connect][tls]") {
    // Prusa hosts: the client asks for 1 KiB records.
    CHECK(input_length_after_handshake(MBEDTLS_SSL_MAX_FRAG_LEN_1024) == 1024);
    // Other hosts (Go/Caddy send up to 16 KiB): the buffer must keep its full size.
    CHECK(input_length_after_handshake(MBEDTLS_SSL_MAX_FRAG_LEN_NONE) == MBEDTLS_SSL_IN_CONTENT_LEN);
}
#endif
