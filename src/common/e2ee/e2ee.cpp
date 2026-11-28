#include "e2ee.hpp"
#include "key.hpp"
#include "sha256_multiuse.hpp"

#include <core/core.hpp>
#include <heap.h>
#include "unique_file_ptr.hpp"
#include <unique_file_ptr.hpp>
#include <sys/stat.h>
#include <common/stat_retry.hpp>

#include <mbedtls/pk.h>
#include <psa/crypto.h>

#include <utility_extensions.hpp>
#include <utils/buffer_reader.hpp>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <span>

using bgcode::core::block_parameters_size;
using bgcode::core::BlockHeader;
using bgcode::core::EBlockType;
using bgcode::core::ECompressionType;
using bgcode::core::EIdentityBlockSignCypher;
using bgcode::core::EIdentityFlags;
using bgcode::core::EKeyBlockEncryption;
using std::unique_ptr;

using string_view_u8 = std::basic_string_view<uint8_t>;

namespace {

std::optional<size_t> get_block_header_bytes(BlockHeader header, uint8_t *buffer, size_t buffer_size) {
    if (buffer_size < header.get_size()) {
        return std::nullopt;
    }
    size_t pos = 0;
    memcpy(&buffer[pos], &header.type, sizeof(header.type));
    pos += sizeof(header.type);
    memcpy(&buffer[pos], &header.compression, sizeof(header.compression));
    pos += sizeof(header.compression);
    memcpy(&buffer[pos], &header.uncompressed_size, sizeof(header.uncompressed_size));
    pos += sizeof(header.uncompressed_size);
    if ((ECompressionType)header.compression != ECompressionType::None) {
        memcpy(&buffer[pos], &header.compressed_size, sizeof(header.compressed_size));
        pos += sizeof(header.compressed_size);
    }
    return pos;
}

bool read_from_file(void *data, size_t data_size, FILE *file) {
    const size_t rsize = fread(data, 1, data_size, file);
    return !ferror(file) && rsize == data_size;
}

} // namespace

namespace e2ee {

IdentityBlockInfo::IdentityBlockInfo()
    : identity_pk(std::make_unique<Pk>()) {}

IdentityBlockInfo::~IdentityBlockInfo() = default;
IdentityBlockInfo::IdentityBlockInfo(IdentityBlockInfo &&other) = default;
IdentityBlockInfo &IdentityBlockInfo::operator=(IdentityBlockInfo &&other) = default;

PrinterPrivateKey::PrinterPrivateKey() = default;

PrinterPrivateKey::~PrinterPrivateKey() {
    if (key) {
        crash_dump::privacy_protection.unreg(key.get());
    }
}

bool is_private_key_present() {
    struct stat st;
    return stat_retry(private_key_path, &st) == 0 && S_ISREG(st.st_mode);
}

std::span<const uint8_t> PrinterPrivateKey::get_printer_private_key() {
    if (key) {
        return { key.get(), key_size };
    }
    std::unique_ptr<uint8_t[]> buffer(new uint8_t[e2ee::PRIVATE_KEY_BUFFER_SIZE]);
    crash_dump::privacy_protection.reg(buffer.get(), e2ee::PRIVATE_KEY_BUFFER_SIZE);
    const auto fail = [&buffer]() {
        crash_dump::privacy_protection.unreg(buffer.get());
        return std::span<const uint8_t> {};
    };
    unique_file_ptr inf(fopen(e2ee::private_key_path, "rb"));
    if (!inf) {
        return fail();
    }

    const size_t ins = fread(buffer.get(), 1, e2ee::PRIVATE_KEY_BUFFER_SIZE, inf.get());
    if (ins == 0 || ferror(inf.get()) || !feof(inf.get())) {
        return fail();
    }
    inf.reset();

    key = std::move(buffer);
    key_size = ins;
    return { key.get(), key_size };
}

bool rsa_sha256_sign_verify(mbedtls_pk_context &pk, const uint8_t *message, size_t message_size, const uint8_t *signature, size_t sig_size) {
    unsigned char hash[HASH_SIZE];

    // Calculate the SHA-256 hash of the message
    size_t hash_len;
    if (psa_hash_compute(PSA_ALG_SHA_256, message, message_size, hash, sizeof hash, &hash_len) != PSA_SUCCESS) {
        return false;
    }

    if (sig_size != (mbedtls_pk_get_bitlen(&pk) + 7) / 8) {
        return false;
    }
    // RSA-PSS with MGF1-SHA-256 and any salt length. The call fails for a key that is not RSA.
    return mbedtls_pk_verify_ext(MBEDTLS_PK_SIGALG_RSA_PSS, &pk, MBEDTLS_MD_SHA256, hash, HASH_SIZE, signature, sig_size) == 0;
}

bool rsa_oaep_decrypt(std::span<const uint8_t> private_key_der, const uint8_t *encrypted_msg, size_t msg_size, uint8_t *output, size_t out_size, size_t &decrypted_size) {
    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    // The key is in the PSA key store only for this call. Import fails for a key that is not RSA.
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_RSA_KEY_PAIR);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_RSA_OAEP(PSA_ALG_SHA_256));
    mbedtls_svc_key_id_t key_id;
    if (psa_import_key(&attributes, private_key_der.data(), private_key_der.size(), &key_id) != PSA_SUCCESS) {
        return false;
    }
    psa_reset_key_attributes(&attributes);
    bool ok = psa_get_key_attributes(key_id, &attributes) == PSA_SUCCESS
        && msg_size == PSA_BITS_TO_BYTES(psa_get_key_bits(&attributes))
        && psa_asymmetric_decrypt(key_id, PSA_ALG_RSA_OAEP(PSA_ALG_SHA_256), encrypted_msg, msg_size, nullptr, 0, output, out_size, &decrypted_size) == PSA_SUCCESS;
    psa_reset_key_attributes(&attributes);
    psa_destroy_key(key_id);
    return ok;
}
const char *read_and_verify_identity_block(FILE *file, const BlockHeader &block_header, uint8_t *computed_intro_hash, IdentityBlockInfo &info, bool verify_signature) {
    uint16_t algo;
    if (!read_from_file(&algo, sizeof(algo), file)) {
        return file_error;
    }
    if (algo != std::to_underlying(EIdentityBlockSignCypher::RSA)) {
        return unknown_identity_cypher;
    }
    uint8_t flags;
    if (!read_from_file(&flags, sizeof(flags), file)) {
        return file_error;
    }
    info.one_time_identity = flags & std::to_underlying(EIdentityFlags::ONE_TIME_IDENTITY);
    if (block_header.compression != std::to_underlying(ECompressionType::None)) {
        return compressed_identity_block;
    }
    size_t block_size = block_header.uncompressed_size;
    uint16_t key_len;
    uint8_t name_len;
    // Keeps block_size - SIGN_SIZE from underflowing the allocation and body read below.
    constexpr size_t MIN_IDENTITY_BLOCK_SIZE
        = SIGN_SIZE + sizeof(key_len) + sizeof(name_len) + 2 * HASH_SIZE;
    if (block_size < MIN_IDENTITY_BLOCK_SIZE) {
        return identity_parsing_error;
    }
    // Cap the allocation: a real identity block cannot exceed its fixed fields
    // plus the largest key and name we accept.
    //
    // (Doesn't need to be exact, only some sane upper bound is enough).
    constexpr size_t MAX_IDENTITY_BLOCK_SIZE
        = SIGN_SIZE + sizeof(key_len) + PUBLIC_KEY_BUFFER_SIZE + sizeof(name_len) + (IDENTITY_NAME_LEN - 1) + 2 * HASH_SIZE;
    if (block_size > MAX_IDENTITY_BLOCK_SIZE) {
        return identity_parsing_error;
    }
    size_t signed_bytes_size = block_header.get_size() + block_parameters_size(EBlockType::IdentityBlock) + block_size - SIGN_SIZE;

    // TODO: Should this be dynamic? fallible or not?
    unique_ptr<uint8_t[]> bytes(new uint8_t[signed_bytes_size]);
    std::optional<size_t> pos_opt = get_block_header_bytes(block_header, bytes.get(), signed_bytes_size);
    if (!pos_opt.has_value()) {
        return identity_parsing_error;
    }
    size_t pos = *pos_opt;
    memcpy(bytes.get() + pos, &algo, sizeof(algo));
    pos += sizeof(algo);
    memcpy(bytes.get() + pos, &flags, sizeof(flags));
    pos += sizeof(flags);
    // Read the data, but not the signature
    if (!read_from_file(&bytes.get()[pos], block_size - SIGN_SIZE, file)) {
        return file_error;
    }
    BufferReader body(std::span<const uint8_t>(bytes.get(), signed_bytes_size).subspan(pos));

    const uint8_t *key_len_ptr = body.read(sizeof(key_len));
    if (key_len_ptr == nullptr) {
        return identity_parsing_error;
    }
    memcpy(&key_len, key_len_ptr, sizeof(key_len));

    const uint8_t *key_ptr = body.read(key_len);
    if (key_ptr == nullptr) {
        return identity_parsing_error;
    }

    const uint8_t *name_len_ptr = body.read(sizeof(name_len));
    if (name_len_ptr == nullptr) {
        return identity_parsing_error;
    }
    memcpy(&name_len, name_len_ptr, sizeof(name_len));

    const uint8_t *name_ptr = body.read(name_len);
    if (name_ptr == nullptr) {
        return identity_parsing_error;
    }

    const uint8_t *intro_hash_ptr = body.read(HASH_SIZE);
    if (intro_hash_ptr == nullptr) {
        return identity_parsing_error;
    }
    const uint8_t *key_block_hash_ptr = body.read(HASH_SIZE);
    if (key_block_hash_ptr == nullptr) {
        return identity_parsing_error;
    }

    if (name_len > IDENTITY_NAME_LEN - 1) {
        return identity_name_too_long;
    }

    // mbedtls 4 parses keys with PSA.
    if (psa_crypto_init() != PSA_SUCCESS || mbedtls_pk_parse_public_key(&info.identity_pk->pk, key_ptr, key_len) != 0) {
        return identity_parsing_error;
    }

    if (verify_signature) {
        uint8_t sign[SIGN_SIZE];
        if (!read_from_file(sign, SIGN_SIZE, file)) {
            return file_error;
        }
        auto res = rsa_sha256_sign_verify(info.identity_pk->pk, bytes.get(), signed_bytes_size, sign, SIGN_SIZE);
        if (!res) {
            return identity_verification_fail;
        }
    }

    memcpy(info.identity_name.data(), name_ptr, name_len);
    info.identity_name[name_len] = '\0';

    if (computed_intro_hash != nullptr) {
        if (memcmp(computed_intro_hash, intro_hash_ptr, HASH_SIZE) != 0) {
            return corrupted_metadata;
        }
    }
    memcpy(info.key_block_hash.data(), key_block_hash_ptr, HASH_SIZE);

    return nullptr;
}

bool SymmetricCipherInfo::extract_keys(uint8_t *key_block, size_t size) {
    if (size != 2 * KEY_SIZE) {
        return false;
    }
    memcpy(keys->encryption_key, key_block, KEY_SIZE);
    memcpy(keys->sign_key, key_block + KEY_SIZE, KEY_SIZE);
    return true;
}

std::optional<SymmetricCipherInfo> decrypt_key_block(FILE *file, const bgcode::core::BlockHeader &block_header, Pk &identity_pk, std::span<const uint8_t> printer_private_key, SHA256MultiuseHash *hash) {
    if (printer_private_key.empty()) {
        return std::nullopt;
    }
    if (block_header.compression != std::to_underlying(ECompressionType::None)) {
        return std::nullopt;
    }
    uint16_t encryption;
    if (!read_from_file(&encryption, sizeof(encryption), file)) {
        return std::nullopt;
    }
    if (hash) {
        hash->update(reinterpret_cast<uint8_t *>(&encryption), sizeof(encryption));
    }
    // early return, so we don't allocate buffers etc.
    if (encryption != std::to_underlying(EKeyBlockEncryption::None)
        && encryption != std::to_underlying(EKeyBlockEncryption::RSA_ENC_SHA256_SIGN)) {
        // We don't understand this algorithm, so the key is certainly not for
        // us. But we still need to update the hash according to it. Do so in
        // reasonable sized chunks, as we don't have an upper bound for a key
        // size of unknown type.
        constexpr size_t STEP_SIZE = 64;
        std::array<uint8_t, STEP_SIZE> buffer;
        size_t left = block_header.uncompressed_size;
        while (left > 0) {
            size_t step = std::min(STEP_SIZE, left);
            left -= step;
            if (!read_from_file(buffer.data(), step, file)) {
                return std::nullopt;
            }
            if (hash) {
                hash->update(buffer.data(), step);
            }
        }
        return std::nullopt;
    }

    if (encryption == std::to_underlying(EKeyBlockEncryption::RSA_ENC_SHA256_SIGN)) {
        // 256 for the encrypted data, 256 for signature
        constexpr size_t KEY_BLOCK_ENC_SIZE = 512;
        if (block_header.uncompressed_size != KEY_BLOCK_ENC_SIZE) {
            return std::nullopt;
        }
        std::array<uint8_t, KEY_BLOCK_ENC_SIZE> buffer;
        if (!read_from_file(buffer.data(), buffer.size(), file)) {
            return std::nullopt;
        }
        if (hash) {
            hash->update(buffer.data(), buffer.size());
        }
        string_view_u8 encrypted_block(buffer.data(), buffer.size() - SIGN_SIZE);
        string_view_u8 sign(buffer.data() + buffer.size() - SIGN_SIZE, SIGN_SIZE);
        if (!rsa_sha256_sign_verify(identity_pk.pk, encrypted_block.data(), encrypted_block.size(), sign.data(), sign.size())) {
            return std::nullopt;
        }
        const size_t correct_decrypted_size = 2 * HASH_SIZE + 2 * KEY_SIZE;
        size_t decrypted_size;
        uint8_t decrypted_key_block[correct_decrypted_size];
        if (!rsa_oaep_decrypt(printer_private_key, encrypted_block.data(), encrypted_block.size(), decrypted_key_block, sizeof(decrypted_key_block), decrypted_size)) {
            return std::nullopt;
        }
        if (decrypted_size != correct_decrypted_size) {
            return std::nullopt;
        }
        // The hashes are of the SubjectPublicKeyInfo DER of each public key.
        Pk printer_public_key;
        if (!public_key_from_private_der(printer_private_key, printer_public_key)) {
            return std::nullopt;
        }
        uint8_t printer_public_key_hash[HASH_SIZE];
        uint8_t identity_public_key_hash[HASH_SIZE];
        const auto hash_public_key = [&buffer](Pk &pk, uint8_t *out) {
            const int ret = mbedtls_pk_write_pubkey_der(&pk.pk, buffer.data(), buffer.size());
            size_t hash_len;
            return ret > 0
                && psa_hash_compute(PSA_ALG_SHA_256, buffer.data() + buffer.size() - ret, ret, out, HASH_SIZE, &hash_len) == PSA_SUCCESS;
        };
        if (!hash_public_key(printer_public_key, printer_public_key_hash) || !hash_public_key(identity_pk, identity_public_key_hash)) {
            return std::nullopt;
        }

        string_view_u8 identity_pub_key_hash_file(decrypted_key_block, HASH_SIZE);
        string_view_u8 printer_pub_key_hash_file(decrypted_key_block + HASH_SIZE, HASH_SIZE);
        if (memcmp(identity_pub_key_hash_file.data(), identity_public_key_hash, HASH_SIZE) != 0) {
            return std::nullopt;
        }
        if (memcmp(printer_pub_key_hash_file.data(), printer_public_key_hash, HASH_SIZE) != 0) {
            return std::nullopt;
        }

        SymmetricCipherInfo keys;
        keys.extract_keys(decrypted_key_block + 2 * HASH_SIZE, 2 * KEY_SIZE);
        return keys;
    } else /*No encryption*/ {
        uint8_t plain_key_block[2 * KEY_SIZE];
        if (block_header.uncompressed_size != sizeof(plain_key_block)) {
            return std::nullopt;
        }
        if (!read_from_file(&plain_key_block, sizeof(plain_key_block), file)) {
            return std::nullopt;
        }
        if (hash) {
            hash->update(plain_key_block, sizeof(plain_key_block));
        }
        SymmetricCipherInfo keys;
        keys.extract_keys(plain_key_block, sizeof(plain_key_block));
        return keys;
    }
}

} // namespace e2ee
