// inih (lib/inih/ini.c) with the firmware configuration: the printer reads
// /usb/prusa_printer_settings.ini from the USB drive with ini_parse(), for the network settings
// (lib/WUI/wui_api.cpp) and for the Prusa Connect settings (src/connect/marlin_printer.cpp).
// Input: the file content. The harness reads it through a FILE and fgets(), as ini_parse() does.
// Properties: section, name and value are NUL-terminated inside the inih limits, and each is a
// substring of the input (inih only trims and cuts the text of a line). The handler then changes
// the value in place, as the network settings handler does with strtok_r() (inih allows that).

#include <ini.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct Input {
    const uint8_t *data;
    size_t size;
};

bool is_substring(const Input &in, const char *s, size_t len) {
    return len == 0 || memmem(in.data, in.size, s, len) != nullptr;
}

int handler(void *user, const char *section, const char *name, const char *value) {
    const Input &in = *static_cast<const Input *>(user);
    if (section == nullptr || name == nullptr || value == nullptr) {
        std::abort(); // INI_ALLOW_NO_VALUE and INI_CALL_HANDLER_ON_NEW_SECTION are off
    }
    const size_t section_len = strnlen(section, INI_MAX_SECTION);
    const size_t name_len = strnlen(name, INI_MAX_NAME);
    const size_t value_len = strnlen(value, INI_MAX_LINE);
    if (section_len >= INI_MAX_SECTION || name_len >= INI_MAX_NAME || value_len >= INI_MAX_LINE) {
        std::abort(); // not terminated inside the limits
    }
    if (!is_substring(in, section, section_len) || !is_substring(in, name, name_len) || !is_substring(in, value, value_len)) {
        std::abort(); // text that is not in the file
    }
    // The network settings handler splits "dns4" in place.
    char *rest = const_cast<char *>(value);
    while (strtok_r(rest, ";", &rest) != nullptr) {
    }
    return 1;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0) {
        return 0; // fmemopen() refuses a buffer of size 0
    }
    FILE *file = fmemopen(const_cast<uint8_t *>(data), size, "r");
    if (file == nullptr) {
        std::abort();
    }
    Input in { data, size };
    (void)ini_parse_file(file, handler, &in);
    fclose(file);
    return 0;
}
