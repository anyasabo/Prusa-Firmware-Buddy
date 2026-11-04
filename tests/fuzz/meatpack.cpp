// MeatPack::handle_rx_char(), fed one byte at a time and drained after each byte, exactly as
// gcode_reader_binary.cpp (stream_getc_decode_meatpacked) uses it.
// The decoder lives on the heap with its output buffer as the last member, so ASan catches an
// overflow of char_out_buf past the end of the object.

#include <meatpack.h>

#include <cstdint>
#include <cstdlib>
#include <memory>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    auto mp = std::make_unique<MeatPack>();
    for (size_t i = 0; i < size; ++i) {
        mp->handle_rx_char(data[i]);
        int drained = 0;
        while (mp->has_result_char()) {
            (void)mp->get_result_char();
            if (++drained > 4) {
                std::abort(); // one input byte yields at most kCharOutBufSize (4) characters
            }
        }
    }
    return 0;
}
