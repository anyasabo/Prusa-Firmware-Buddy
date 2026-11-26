// The write timeout of HttpClient when ticks_ms() wraps around (each 2^32 ms, 49.7 days).
//
// The test binary links with -Wl,--wrap=ticks_ms (CMakeLists.txt), so the calls in httpc.cpp
// come to __wrap_ticks_ms() and read the mock clock.

#include <http/httpc.hpp>
#include <http/connection.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

using namespace http;

namespace {

uint32_t mock_now_ms = 0;

/// Accepts at most one byte for each tx() call and advances the mock clock in each call.
/// After max_calls, it fails with Error::Network, so that a loop that never times out ends.
class SlowConnection final : public Connection {
public:
    SlowConnection(uint32_t ms_per_call, bool accept)
        : Connection(5)
        , ms_per_call(ms_per_call)
        , accept(accept) {}
    uint32_t ms_per_call;
    bool accept;
    size_t calls = 0;
    static constexpr size_t max_calls = 2000;
    std::string sent;

    std::variant<size_t, Error> rx(uint8_t *, size_t, bool) override {
        return Error::Network;
    }
    std::variant<size_t, Error> tx(const uint8_t *buffer, size_t len) override {
        if (++calls > max_calls) {
            return Error::Network;
        }
        mock_now_ms += ms_per_call;
        if (!accept || len == 0) {
            return static_cast<size_t>(0);
        }
        sent += static_cast<char>(buffer[0]);
        return static_cast<size_t>(1);
    }
    bool poll_readable(uint32_t) override {
        return false;
    }
};

class Factory final : public ConnectionFactory {
public:
    Connection *conn;
    Factory(Connection *conn)
        : conn(conn) {}
    std::variant<Connection *, Error> connection() override {
        return conn;
    }
    const char *host() override {
        return "example.com";
    }
    void invalidate() override {}
};

class GetRequest final : public Request {
public:
    const char *url() const override {
        return "/index.html";
    }
    ContentType content_type() const override {
        return ContentType::ApplicationJson;
    }
    Method method() const override {
        return Method::Get;
    }
};

} // namespace

extern "C" uint32_t __wrap_ticks_ms() {
    return mock_now_ms;
}

TEST_CASE("httpc: a slow write that makes progress does not time out when the clock wraps", "[http][timeout]") {
    // Each write takes 100 ms and sends one byte. The timeout is 5 s for each write, so the
    // request must go out. The clock wraps during the request.
    mock_now_ms = UINT32_MAX - 2000;
    SlowConnection conn(100, true);
    Factory factory(&conn);
    HttpClient client(factory);
    GetRequest request;

    // There is no response (rx() fails), so send() fails after the request. It must not fail
    // with a timeout while it writes the request.
    auto resp = client.send(request);
    REQUIRE(std::holds_alternative<Error>(resp));
    CHECK(std::get<Error>(resp) != Error::Timeout);
    CHECK(conn.sent.starts_with("GET /index.html HTTP/1.1\r\n"));
    CHECK(conn.sent.ends_with("\r\n\r\n"));
}

TEST_CASE("httpc: a stalled write times out when the clock wraps", "[http][timeout]") {
    // No write makes progress, and each write takes 1 s. After more than 5 s, the request
    // must stop with a timeout. The clock wraps during the wait.
    mock_now_ms = UINT32_MAX - 5500;
    SlowConnection conn(1000, false);
    Factory factory(&conn);
    HttpClient client(factory);
    GetRequest request;

    auto resp = client.send(request);
    REQUIRE(std::holds_alternative<Error>(resp));
    CHECK(std::get<Error>(resp) == Error::Timeout);
    CHECK(conn.calls <= 7);
}
