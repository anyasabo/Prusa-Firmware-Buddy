// Untrusted print files (USB stick, PrusaLink upload, Connect download) through the firmware's own
// reader: AnyGcodeFormatReader picks plain or binary G-code from the file name and contents, then
// we drive every consumer the printer runs on a file: validity check, index generation, metadata,
// thumbnails of the sizes the GUI asks for, and the full G-code stream.
//
// Input: byte 0 = flags (bit 0: binary vs plain extension, bit 1: allow decryption,
//        bit 2: ignore CRC when streaming G-code, bit 3: stream through the generated index as
//        gcode_info.cpp does), rest = file contents.
//
// Streams are drained until they stop returning OK; a reader that never stops is reported by
// libFuzzer's -timeout (and by the ctest timeout in replay mode), not hidden by a cap.
// Decryption needs the test printer key, which the reader loads from the working directory
// (e2ee.hpp private_key_path). CMake copies printer_private_key.der next to the binary: run the
// fuzzer from there (ctest does).

#include <gcode/gcode_reader_any.hpp>
#include <gcode/gcode_buffer.hpp>

#include <utils/abstract_byte_reader.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

namespace {

const std::string &temp_path(bool binary) {
    static const std::string bin = "/tmp/fuzz_bgcode_reader_" + std::to_string(getpid()) + ".bgcode";
    static const std::string txt = "/tmp/fuzz_bgcode_reader_" + std::to_string(getpid()) + ".gcode";
    // Registered after both are constructed, so it runs before they are destroyed.
    static const bool cleanup = (std::atexit([] { std::remove(bin.c_str()); std::remove(txt.c_str()); }), true);
    (void)cleanup;
    return binary ? bin : txt;
}

void drain_lines(IGcodeReader &r) {
    GcodeBuffer buffer;
    while (r.stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_OK) {
    }
}

// Thumbnails are read through the returned AbstractByteReader, as window_thumbnail.cpp (display)
// and connect/render.cpp (Connect preview) do.
void drain_thumbnail(AbstractByteReader &reader) {
    std::array<std::byte, 64> chunk;
    while (reader.read(chunk).size() == chunk.size()) {
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) {
        return 0;
    }
    const uint8_t flags = data[0];
    const bool binary = flags & 1;
    const bool allow_decryption = flags & 2;
    const bool ignore_crc = flags & 4;
    const bool use_index = flags & 8;

    const std::string &path = temp_path(binary);
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
        return 0;
    }
    fwrite(data + 1, 1, size - 1, f);
    fclose(f);

    AnyGcodeFormatReader reader(path.c_str(), allow_decryption);
    if (!reader.is_open()) {
        return 0;
    }
    IGcodeReader &r = *reader.get();

    (void)r.valid_for_print(true);

    IGcodeReader::Index index;
    index.thumbnails[0] = { 440, 240, IGcodeReader::ImgType::PNG };
    index.thumbnails[1] = { 16, 16, IGcodeReader::ImgType::QOI };
    r.generate_index(index, ignore_crc);

    if (r.stream_metadata_start(use_index ? &index : nullptr)) {
        drain_lines(r);
    }
    for (const auto &[w, h, type] : { std::tuple { 440, 240, IGcodeReader::ImgType::PNG },
             std::tuple { 16, 16, IGcodeReader::ImgType::QOI },
             std::tuple { 313, 173, IGcodeReader::ImgType::QOI } }) {
        if (AbstractByteReader *thumb = r.stream_thumbnail_start(w, h, type, /*allow_larger=*/true)) {
            drain_thumbnail(*thumb);
        }
    }
    if (r.stream_gcode_start(0, ignore_crc, use_index ? &index : nullptr) == IGcodeReader::Result_t::RESULT_OK) {
        drain_lines(r);
    }
    return 0;
}
