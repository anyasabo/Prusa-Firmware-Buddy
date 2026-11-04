// http::url_decode(url, out, out_len): PrusaLink decodes request paths with it before any
// authentication. Input: byte 0 = output buffer size (0..255), rest = the URL.
// The output buffer is heap-allocated at exactly that size, so ASan catches any write past it.
// Property: on success the result is NUL-terminated inside the buffer.

#include <http/url_decode.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) {
        return 0;
    }
    const size_t out_len = data[0];
    const std::string_view url(reinterpret_cast<const char *>(data + 1), size - 1);
    std::unique_ptr<char[]> out(new char[out_len]);
    if (http::url_decode(url, out.get(), out_len)) {
        if (out_len == 0 || memchr(out.get(), '\0', out_len) == nullptr) {
            std::abort(); // reported success without a terminated result inside the buffer
        }
    }
    return 0;
}
