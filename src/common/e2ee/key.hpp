#include "e2ee.hpp"
#include <mbedtls/pk.h>

#include <span>

class AsyncJobExecutionControl;

namespace e2ee {
struct Pk {
    mbedtls_pk_context pk;
    Pk();
    ~Pk();
    Pk(const Pk &) = delete;
    Pk &operator=(const Pk &) = delete;
};

/// Puts the public part of an RSA private key (PKCS#1 DER) into \p public_key.
[[nodiscard]] bool public_key_from_private_der(std::span<const uint8_t> private_key_der, Pk &public_key);

// Note: This struct is written to file for the key, only add members
//  never remove, so we don't lose backwards compatibility
struct __attribute__((packed)) IdentityKeyInfo {
    uint8_t version { 1 };
    char identity_name[IDENTITY_NAME_LEN];
};

void generate_key(AsyncJobExecutionControl &control, bool &result);
void remove_key();

bool export_key();

bool save_identity_key(const IdentityInfo &info);
bool save_identity_key_temporary(const IdentityInfo &info);

bool is_trusted_identity(const IdentityInfo &info);

void remove_trusted_identity(const IdentityInfo &info);
void remove_temporary_identites();
void remove_all_identities();

void get_key_hash_string(char *out, size_t size, e2ee::Pk *pk);

} // namespace e2ee
