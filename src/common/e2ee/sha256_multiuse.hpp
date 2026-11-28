#include "e2ee.hpp"
#include <psa/crypto.h>

namespace e2ee {

class SHA256MultiuseHash {
public:
    SHA256MultiuseHash();

    ~SHA256MultiuseHash();

    /// Writes the hash of the data since the last call and starts a new hash. Returns false if
    /// the hash failed.
    [[nodiscard]] bool get_hash(uint8_t *buffer, size_t buffer_size);

    void update(const uint8_t *data, size_t size);

private:
    psa_hash_operation_t sha256_ctx = PSA_HASH_OPERATION_INIT;
    void reset();
};

} // namespace e2ee
