#pragma once

#include <cstdint>
#include <span>

namespace connect_client {

using Der = std::span<const uint8_t>;

/// Whether the host is prusa3d.com or a subdomain of it (case-insensitive).
bool is_prusa_host(const char *host);

/// The built-in root certificates (DER) that the client trusts for a connection to the host.
///
/// A Prusa host gets only the Prusa CA, so that a certificate from a public CA cannot
/// impersonate Prusa Connect. Another host (a self-hosted Connect) also gets the public
/// roots, if the printer has them.
std::span<const Der> trust_anchors(const char *host);

} // namespace connect_client
