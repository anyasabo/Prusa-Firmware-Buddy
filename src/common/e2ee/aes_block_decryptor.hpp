#pragma once
#include <psa/crypto.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace e2ee {

/// AES decryption of single 16-byte blocks (ECB), the base of the CBC mode in Decryptor.
///
/// The key is in the PSA key store only during set_key(). After that, the PSA operation keeps
/// its own copy of the expanded key on the heap.
class AesBlockDecryptor {
public:
    static constexpr size_t BLOCK_SIZE = 16;
    using Block = std::array<uint8_t, BLOCK_SIZE>;

    AesBlockDecryptor() = default;
    AesBlockDecryptor(const AesBlockDecryptor &) = delete;
    AesBlockDecryptor &operator=(const AesBlockDecryptor &) = delete;
    AesBlockDecryptor(AesBlockDecryptor &&) = delete;
    AesBlockDecryptor &operator=(AesBlockDecryptor &&) = delete;
    ~AesBlockDecryptor();

    /// Sets an AES-128 key. Returns false on failure (out of memory). Then decrypt_block() fails
    /// until the next successful set_key().
    [[nodiscard]] bool set_key(const uint8_t *key, size_t key_size);
    [[nodiscard]] bool decrypt_block(const Block &in, Block &out);

private:
    psa_cipher_operation_t operation = PSA_CIPHER_OPERATION_INIT;
};

} // namespace e2ee
