#pragma once

#include <common/http/socket.hpp>

namespace connect_client {

/// The TCP connection under a TLS connection, and the send and receive callbacks for mbedtls_ssl_set_bio().
struct NetContext {
    http::socket_con plain_conn;
    bool timeout_happened;
    NetContext(uint8_t timeout_s);
    NetContext(const NetContext &other) = delete;
    NetContext(NetContext &&other) = delete;
    NetContext &operator=(const NetContext &other) = delete;
    NetContext &operator=(NetContext &&other) = delete;
};

/// Connects the TCP connection. Returns 0 or an MBEDTLS_ERR_NET_ error.
int net_connect(NetContext *ctx, const char *host, uint16_t port);
/// The send callback for mbedtls_ssl_set_bio(). ctx is a NetContext.
int net_send(void *ctx, const unsigned char *buf, size_t len);
/// The receive callback for mbedtls_ssl_set_bio(). ctx is a NetContext.
int net_recv(void *ctx, unsigned char *buf, size_t len);

} // namespace connect_client
