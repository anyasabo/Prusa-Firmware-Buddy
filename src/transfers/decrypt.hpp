#pragma once

#include <array>
#include <tuple>
#include <cstdint>
#include <cstdlib>

#include <psa/crypto.h>

namespace transfers {

class Decryptor {
public:
    static constexpr uint32_t BlockSize = 16;
    static constexpr uint32_t CtrCounterSize = 4;
    using Block = std::array<uint8_t, BlockSize>;

private:
    // We now use the CTR mode of aes. That one is a stream cipher (doesn't
    // need to be block-padded). Nevertheless, for historical reasons, we _do_
    // the block padding on the sender side and cut it off here by the expected
    // size.
    size_t size_left;
    // PSA does not allow to move an operation. Therefore, the whole Decryptor is not movable
    // _on purpose_.
    psa_cipher_operation_t operation = PSA_CIPHER_OPERATION_INIT;

public:
    /// If the setup of the cipher fails (out of memory), decrypt() causes a BSOD, as for any other
    /// decryption failure.
    Decryptor(const Block &key, const Block &nonce, size_t offset, size_t size_left);
    Decryptor(const Decryptor &other) = delete;
    Decryptor(Decryptor &&other) = delete;
    Decryptor &operator=(const Decryptor &other) = delete;
    Decryptor &operator=(Decryptor &&other) = delete;
    ~Decryptor();

    // Decrypts some data from in-buffer to the out-buffer.
    //
    // Returns how much was consumed of each buffer. Will make at least some progress.
    std::tuple<size_t, size_t> decrypt(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size);
};

} // namespace transfers
