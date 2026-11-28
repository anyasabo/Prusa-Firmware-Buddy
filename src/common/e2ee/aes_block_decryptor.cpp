#include "aes_block_decryptor.hpp"

namespace e2ee {

AesBlockDecryptor::~AesBlockDecryptor() {
    psa_cipher_abort(&operation);
}

bool AesBlockDecryptor::set_key(const uint8_t *key, size_t key_size) {
    psa_cipher_abort(&operation);
    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECB_NO_PADDING);
    mbedtls_svc_key_id_t key_id;
    if (psa_import_key(&attributes, key, key_size, &key_id) != PSA_SUCCESS) {
        return false;
    }
    const psa_status_t status = psa_cipher_decrypt_setup(&operation, key_id, PSA_ALG_ECB_NO_PADDING);
    psa_destroy_key(key_id);
    return status == PSA_SUCCESS;
}

bool AesBlockDecryptor::decrypt_block(const Block &in, Block &out) {
    size_t out_len;
    return psa_cipher_update(&operation, in.data(), in.size(), out.data(), out.size(), &out_len) == PSA_SUCCESS
        && out_len == out.size();
}

} // namespace e2ee
