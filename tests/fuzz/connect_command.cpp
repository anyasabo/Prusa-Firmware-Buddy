// Commands from the Connect server (or anyone who can MITM it when TLS is off): the body of a
// JSON command goes through jsmn + json::search + per-command argument parsing, a G-code command
// is taken verbatim. Input: byte 0 bit 0 selects JSON (1) or G-code (0); rest = body.
// The body is copied into an exactly-sized heap buffer (parse_json_command unescapes in place).
//
// Known issue: the argument copies use strlcpy(dst, value.data(), len), and strlcpy() strlen()s the
// whole source, so it reads past the end of the body. On the printer the body sits in a stack
// buffer filled by read_all() with no terminator. By default a NUL follows the body here so
// fuzzing gets past that; BUDDY_FUZZ_STRICT=1 reproduces the printer's layout.

#include <command.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>

using namespace connect_client;

namespace {
SharedBuffer buffer;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) {
        return 0;
    }
    const bool json = data[0] & 1;
    const size_t body_size = size - 1;
    static const bool strict = getenv("BUDDY_FUZZ_STRICT") != nullptr;
    std::unique_ptr<char[]> body(new char[body_size + (strict ? 0 : 1) + (body_size == 0)]);
    memcpy(body.get(), data + 1, body_size);
    if (!strict) {
        body[body_size] = '\0';
    }

    auto borrow = buffer.borrow();
    if (!borrow.has_value()) {
        __builtin_trap(); // a previous command leaked its buffer borrow
    }
    if (json) {
        const auto command = Command::parse_json_command(13, body.get(), body_size, std::move(*borrow));
        (void)command;
    } else {
        const auto command = Command::gcode_command(13, std::string_view(body.get(), body_size), std::move(*borrow));
        (void)command;
    }
    return 0;
}
