#include "trust_store.hpp"
#include "certificate.h"

#include <option/has_tls_public_ca.h>

#include <array>
#include <cstring>
#include <strings.h>

#if HAS_TLS_PUBLIC_CA()
    #include "public_ca.h"
#endif

namespace connect_client {

namespace {

    constexpr const char *prusa_domain = "prusa3d.com";

    constexpr std::array<Der, 1> prusa_anchors = {
        Der { ca_cert_der, ca_cert_der_len },
    };

#if HAS_TLS_PUBLIC_CA()
    constexpr std::array<Der, 3> all_anchors = {
        Der { ca_cert_der, ca_cert_der_len },
        Der { isrg_root_x2_der, sizeof isrg_root_x2_der },
        Der { isrg_root_ye_der, sizeof isrg_root_ye_der },
    };
#endif

} // namespace

bool is_prusa_host(const char *host) {
    const size_t len = strlen(host);
    const size_t domain_len = strlen(prusa_domain);
    if (len < domain_len || strcasecmp(host + len - domain_len, prusa_domain) != 0) {
        return false;
    }
    // Only whole labels: "connect.prusa3d.com" yes, "notprusa3d.com" no.
    return len == domain_len || host[len - domain_len - 1] == '.';
}

std::span<const Der> trust_anchors([[maybe_unused]] const char *host) {
#if HAS_TLS_PUBLIC_CA()
    if (!is_prusa_host(host)) {
        return all_anchors;
    }
#endif
    return prusa_anchors;
}

} // namespace connect_client
