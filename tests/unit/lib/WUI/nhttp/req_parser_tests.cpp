#include <catch2/catch_test_macros.hpp>
#include <nhttp/req_parser.h>
#include <nhttp/server.h>
#include <nhttp/handler.h>
#include <http/types.h>

#include <iterator>
#include <string.h>

using http::MAX_URL_LEN;
using nhttp::Server;
using nhttp::ServerDefs;
using nhttp::handler::RequestParser;
using nhttp::handler::Selector;

class EmptyServerDefs : public ServerDefs {
public:
    virtual const Selector *const *selectors() const override { return {}; }
    virtual const char *get_password() const override { return ""; }
    virtual altcp_pcb *listener_alloc() const override { return {}; }
};

class TestRequestParser : public RequestParser {
public:
    TestRequestParser(Server &server)
        : RequestParser(server) {};

    void set_url(const char *url) {
        url_size = strlen(url);
        strcpy(this->url.data(), url);
    }
};

TEST_CASE("url parser", "[url]") {
    EmptyServerDefs server_defs;
    Server server(server_defs);
    TestRequestParser parser(server);
    char url[MAX_URL_LEN];

    SECTION("small buffer") {
        char small_buffer[1];
        parser.set_url("/test/api/file.gcode");
        REQUIRE_FALSE(parser.uri_filename(small_buffer, std::size(small_buffer)));
    }

    SECTION("url with encoded and real ?") {
        parser.set_url("/test/api/file%3f.gcode?somemorestuff");
        REQUIRE(parser.uri_filename(url, std::size(url)));
        REQUIRE(strcmp(url, "/test/api/file?.gcode") == 0);
    }

    SECTION("double dot") {
        parser.set_url("/test/api/../file.gcode");
        REQUIRE_FALSE(parser.uri_filename(url, std::size(url)));
    }

    SECTION("encoded double dot") {
        parser.set_url("/test/api/%2e%2e/file.gcode");
        REQUIRE_FALSE(parser.uri_filename(url, std::size(url)));
    }

    SECTION("trailing double dot slash") {
        parser.set_url("/usb/../");
        REQUIRE_FALSE(parser.uri_filename(url, std::size(url)));
    }

    SECTION("bare double dot slash") {
        parser.set_url("/../");
        REQUIRE_FALSE(parser.uri_filename(url, std::size(url)));
    }
}

TEST_CASE("Expect: 100-continue", "[req_parser]") {
    EmptyServerDefs server_defs;
    Server server(server_defs);
    TestRequestParser parser(server);

    const auto feed = [&](const char *request) {
        for (const char *c = request; *c; c++) {
            parser.feed(static_cast<uint8_t>(*c));
        }
    };

    SECTION("present") {
        feed("PUT /api/v1/files/usb/a.gcode HTTP/1.1\r\nExpect: 100-continue\r\nContent-Length: 5\r\n\r\n");
        REQUIRE(parser.expect_continue);
        REQUIRE(parser.content_length == 5);
    }

    SECTION("absent") {
        feed("PUT /api/v1/files/usb/a.gcode HTTP/1.1\r\nContent-Length: 5\r\n\r\n");
        REQUIRE_FALSE(parser.expect_continue);
    }

    SECTION("other expectation") {
        feed("PUT /api/v1/files/usb/a.gcode HTTP/1.1\r\nExpect: something-else\r\nContent-Length: 5\r\n\r\n");
        REQUIRE_FALSE(parser.expect_continue);
        REQUIRE(parser.content_length == 5);
    }
}
