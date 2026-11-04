// GCodeParser2 over arbitrary lines, then every option letter read as each supported type,
// including string values into a deliberately small heap buffer (ASan catches overruns).
// Input: lines separated by '\n'; byte 0 of the input sets the string buffer size (0..63).

#include <gcode_parser.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) {
        return 0;
    }
    const size_t buf_size = data[0] & 63;
    std::string_view input(reinterpret_cast<const char *>(data + 1), size - 1);
    std::unique_ptr<char[]> buf(new char[buf_size ? buf_size : 1]);

    GCodeParser2 p([](const GCodeBasicParser::ErrorCallbackArgs &, va_list) {});
    while (!input.empty()) {
        const size_t nl = input.find('\n');
        const std::string_view line = input.substr(0, nl);
        input = nl == std::string_view::npos ? std::string_view {} : input.substr(nl + 1);
        // The printer never hands the parser more than MAX_CMD_SIZE (96) characters per line, and
        // option positions are stored as uint8_t (only debug_assert-guarded), so skip longer ones.
        if (line.size() > 96 || !p.parse(line)) {
            continue;
        }
        for (char key = GCodeParser2::first_option_letter; key <= GCodeParser2::last_option_letter; ++key) {
            if (!p.has_option(key)) {
                continue;
            }
            (void)p.option<float>(key);
            (void)p.option<int32_t>(key);
            (void)p.option<uint32_t>(key);
            (void)p.option<uint8_t>(key);
            (void)p.option<bool>(key);
            (void)p.option<std::string_view>(key, std::span<char>(buf.get(), buf_size));
        }
    }
    return 0;
}
