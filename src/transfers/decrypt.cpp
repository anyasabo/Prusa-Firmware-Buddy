#include "decrypt.hpp"

#include <common/bsod.h>
#include <logging/log.hpp>

#include <type_traits>
#include <cstring>

using std::make_tuple;
using std::min;
using std::tuple;

LOG_COMPONENT_REF(connect);

namespace transfers {

Decryptor::Decryptor(const Block &key, const Block &nonce, size_t offset, size_t size_left)
    : size_left(size_left) {
    debug_assert(offset % BlockSize == 0);

    /// Set counter to the block index we are going to decrypt
    Block counter = nonce;
    uint32_t block_idx = offset / BlockSize;
    for (uint32_t i = BlockSize - 1; i >= BlockSize - CtrCounterSize; i--) {
        counter[i] = block_idx & 0xff;
        block_idx >>= 8;
    }

    if (psa_crypto_init() != PSA_SUCCESS) {
        return;
    }
    // The key is in the PSA key store only during the setup. The operation keeps its own copy.
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_CTR);
    mbedtls_svc_key_id_t key_id;
    if (psa_import_key(&attributes, key.data(), key.size(), &key_id) != PSA_SUCCESS) {
        return;
    }
    if (psa_cipher_decrypt_setup(&operation, key_id, PSA_ALG_CTR) == PSA_SUCCESS
        && psa_cipher_set_iv(&operation, counter.data(), counter.size()) != PSA_SUCCESS) {
        psa_cipher_abort(&operation);
    }
    psa_destroy_key(key_id);
}

Decryptor::~Decryptor() {
    psa_cipher_abort(&operation);
}

tuple<size_t, size_t> Decryptor::decrypt(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size) {
    size_t amnt = min(size_left, min(in_size, out_size));
    size_left -= amnt;
    size_t out_len = 0;
    // CTR is a stream mode: PSA decrypts all the input at once and keeps the rest of the key
    // stream block for the next call.
    auto ret = psa_cipher_update(&operation, in, amnt, out, amnt, &out_len);
    if (ret != PSA_SUCCESS || out_len != amnt) {
        // There should be no failure mode in real life for decryption with
        // correct parameters, but we are super-careful and catch any
        // unexpected return values because handling encrypted data is
        // sensitive thing at best.
        bsod("Decryption failed");
    }
    return make_tuple(size_left ? amnt : in_size /* Discard the rest of input when everything consumed */, amnt);
}

} // namespace transfers
