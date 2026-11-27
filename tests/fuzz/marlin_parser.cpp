// The Marlin G-code parser (lib/Marlin/Marlin/src/gcode/parser.cpp) over arbitrary lines, then each
// parameter letter read with each accessor that the handlers use. Each line is in a heap buffer of
// the exact size, as the printer gives it to the parser, so AddressSanitizer finds a read past the
// end. Input: lines separated by '\n'.

#include <parser.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    std::string_view input(reinterpret_cast<const char *>(data), size);
    while (!input.empty()) {
        const size_t nl = input.find('\n');
        std::string_view text = input.substr(0, nl);
        input = nl == std::string_view::npos ? std::string_view {} : input.substr(nl + 1);
        // The printer gives the parser at most MAX_CMD_SIZE - 1 (95) characters with no NUL in them.
        text = text.substr(0, std::min(text.find('\0'), size_t { 95 }));
        std::unique_ptr<char[]> line(new char[text.size() + 1]);
        memcpy(line.get(), text.data(), text.size());
        line[text.size()] = '\0';

        parser.parse(line.get());
        if (parser.string_arg) {
            (void)strlen(parser.string_arg);
        }
        for (char c = 'A'; c <= 'Z'; ++c) {
            if (!parser.seen(c)) {
                continue;
            }
            (void)parser.has_value();
            (void)parser.value_float();
            (void)parser.value_long();
            (void)parser.value_ulong();
            (void)parser.value_ulong(16);
            (void)parser.value_byte();
            (void)parser.value_bool();
            (void)parser.value_millis_from_seconds();
            (void)parser.floatval(c);
            (void)parser.longval(c);
        }
        (void)parser.seen_axis();
    }
    return 0;
}
