#include "key.hpp"
#include "e2ee.hpp"

#include <stat_retry.hpp>
#include <path_utils.h>
#include <common/directory.hpp>

#include <sys/stat.h>
#include <unique_file_ptr.hpp>
#include <raii/deleter.hpp>
#include <heap.h>
#include <psa/crypto.h>
#include <memory>
#include <cstring>
#include <bsod/bsod.h>

using std::unique_ptr;

extern "C" {
size_t strlcpy(char *, const char *, size_t);
size_t strlcat(char *, const char *, size_t);
}

namespace {

bool save_identity_key_impl(const e2ee::IdentityInfo &info, const char *folder) {
    e2ee::IdentityKeyInfo key_info;
    strlcpy(key_info.identity_name, info.identity_name.data(), e2ee::IDENTITY_NAME_LEN);
    constexpr size_t buff_size = std::max(e2ee::IDENTITY_TMP_PATH_LEN, e2ee::IDENTITY_PATH_LEN);
    [[maybe_unused]] char file_path[buff_size];
    strlcpy(file_path, folder, buff_size);
    make_dirs(file_path);
    strlcat(file_path, info.key_hash_str.data(), buff_size);

    // check if it already exists
    if (file_exists(file_path)) {
        debug_assert(false);
        return false;
    }

    unique_file_ptr file(fopen(file_path, "w"));
    if (!file) {
        return false;
    }

    if (fwrite(&key_info, sizeof(key_info), 1, file.get()) != 1) {
        return false;
    }
    return true;
}

void remove_identities_in(const char *in_path) {
    struct dirent *entry;
    char path[e2ee::IDENTITY_PATH_MAX_LEN];
    Directory dir(in_path);
    if (!dir) {
        //????
        return;
    }

    while ((entry = dir.read()) != nullptr) {
        // Ignore "." and ".." (current and parent directories)
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        strlcpy(path, in_path, e2ee::IDENTITY_PATH_MAX_LEN);
        strlcat(path, entry->d_name, e2ee::IDENTITY_PATH_MAX_LEN);

        if (entry->d_type == DT_REG) {
            remove(path);
        }
    }
}

} // namespace

namespace e2ee {

Pk::Pk() {
    mbedtls_pk_init(&pk);
}

Pk::~Pk() {
    mbedtls_pk_free(&pk);
}

bool public_key_from_private_der(std::span<const uint8_t> private_key_der, Pk &public_key) {
    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_RSA_KEY_PAIR);
    mbedtls_svc_key_id_t key_id;
    if (psa_import_key(&attributes, private_key_der.data(), private_key_der.size(), &key_id) != PSA_SUCCESS) {
        return false;
    }
    const int ret = mbedtls_pk_copy_public_from_psa(key_id, &public_key.pk);
    psa_destroy_key(key_id);
    return ret == 0;
}

void generate_key(AsyncJobExecutionControl &control, bool &result) {
    result = false;
    unique_ptr<uint8_t, FreeDeleter> buffer(reinterpret_cast<uint8_t *>(malloc_fallible(PRIVATE_KEY_BUFFER_SIZE)));
    if (!buffer) {
        return;
    }
    crash_dump::ManualSecret secret(buffer.get(), PRIVATE_KEY_BUFFER_SIZE);

    size_t export_size = 0;
    {
        if (psa_crypto_init() != PSA_SUCCESS) {
            return;
        }
        // The public exponent is 65537, the default of PSA.
        psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
        psa_set_key_type(&attributes, PSA_KEY_TYPE_RSA_KEY_PAIR);
        psa_set_key_bits(&attributes, 2048);
        psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_EXPORT);
        mbedtls_svc_key_id_t key_id;
        if (psa_generate_key(&attributes, &key_id) != PSA_SUCCESS) {
            return;
        }
        // The PKCS#1 RSAPrivateKey DER, the same format that earlier firmware wrote.
        const psa_status_t status = psa_export_key(key_id, buffer.get(), PRIVATE_KEY_BUFFER_SIZE, &export_size);
        psa_destroy_key(key_id);
        if (status != PSA_SUCCESS) {
            return;
        }
    } // Free the key, better chance to have RAM for the file

    if (control.is_discarded()) {
        // We aborted the generation, so do not save the key
        return;
    }

    make_dirs(private_key_path);
    unique_file_ptr fout(fopen(private_key_path, "wb"));
    if (!fout) {
        return;
    }

    if (fwrite(buffer.get(), export_size, 1, fout.get()) != 1) {
        return;
    }

    result = true;
    return;
}

bool export_key() {
    unique_ptr<uint8_t, FreeDeleter> buffer(reinterpret_cast<uint8_t *>(malloc_fallible(PRIVATE_KEY_BUFFER_SIZE)));
    if (!buffer) {
        return false;
    }
    crash_dump::ManualSecret secret(buffer.get(), PRIVATE_KEY_BUFFER_SIZE);

    unique_file_ptr inf(fopen(private_key_path, "rb"));
    if (!inf) {
        return false;
    }

    size_t ins = fread(buffer.get(), 1, PRIVATE_KEY_BUFFER_SIZE, inf.get());
    if (ins == 0 || ferror(inf.get()) || !feof(inf.get())) {
        return false;
    }

    inf.reset();

    int ret = 0;
    {
        Pk pk;
        if (!public_key_from_private_der(std::span(buffer.get(), ins), pk)) {
            return false;
        }

        ret = mbedtls_pk_write_pubkey_der(&pk.pk, buffer.get(), PRIVATE_KEY_BUFFER_SIZE);

        if (ret <= 0) {
            return false;
        }
    } // Destroy the pk

    unique_file_ptr outf(fopen(public_key_path, "wb"));
    if (!outf) {
        return false;
    }

    // Note: mbedtls writes to the _end_ of the buffer.
    if (fwrite(buffer.get() + PRIVATE_KEY_BUFFER_SIZE - ret, ret, 1, outf.get()) != 1) {
        outf.reset();
        // Result not checked - no way we can fail twice anyway.
        remove(public_key_path);
        return false;
    }

    return true;
}

void get_key_hash_string(char *out, [[maybe_unused]] size_t size, e2ee::Pk *pk) {
    debug_assert(size >= e2ee::KEY_HASH_STR_BUFFER_LEN);
    uint8_t key_hash[e2ee::HASH_SIZE];
    std::array<uint8_t, e2ee::PUBLIC_KEY_BUFFER_SIZE> buffer;
    const int ret = mbedtls_pk_write_pubkey_der(&pk->pk, buffer.data(), e2ee::PUBLIC_KEY_BUFFER_SIZE);
    size_t hash_len;
    if (ret <= 0 || psa_hash_compute(PSA_ALG_SHA_256, buffer.data() + buffer.size() - ret, ret, key_hash, sizeof key_hash, &hash_len) != PSA_SUCCESS) {
        // No identity file has this name, so the identity is not trusted.
        strlcpy(out, "invalid", size);
        return;
    }

    for (size_t i = 0; i < e2ee::HASH_SIZE; i++) {
        sprintf(&out[i * 2], "%02x", key_hash[i]);
    }
}

bool save_identity_key(const IdentityInfo &info) {
    return save_identity_key_impl(info, identities_folder);
}

bool save_identity_key_temporary(const IdentityInfo &info) {
    return save_identity_key_impl(info, identities_tmp_folder);
}

void remove_trusted_identity(const IdentityInfo &info) {
    char file_path[IDENTITY_PATH_LEN];
    strlcpy(file_path, identities_folder, IDENTITY_PATH_LEN);
    strlcat(file_path, info.key_hash_str.data(), IDENTITY_PATH_LEN);
    debug_assert(file_exists(file_path));
    remove(file_path);
}

void remove_temporary_identites() {
    remove_identities_in(identities_tmp_folder);
}

void remove_key() {
    // We ignore non-existence.
    remove(private_key_path);
}

void remove_all_identities() {
    remove_temporary_identites();
    remove_identities_in(identities_folder);
}

bool is_trusted_identity(const IdentityInfo &info) {
    char file_path[e2ee::IDENTITY_PATH_MAX_LEN];
    // do we trust it permanently?
    strlcpy(file_path, identities_folder, e2ee::IDENTITY_PATH_MAX_LEN);
    strlcat(file_path, info.key_hash_str.data(), e2ee::IDENTITY_PATH_MAX_LEN);
    bool trusted = file_exists(file_path);
    if (!trusted) {
        // or at least temporarily
        strlcpy(file_path, identities_tmp_folder, e2ee::IDENTITY_PATH_MAX_LEN);
        strlcat(file_path, info.key_hash_str.data(), e2ee::IDENTITY_PATH_MAX_LEN);
        trusted = file_exists(file_path);
    }
    return trusted;
}

} // namespace e2ee
