
namespace {

constexpr static const char *PLAIN_TEST_FILE = "test_plain.gcode";
constexpr static const char *BINARY_NO_COMPRESSION_FILE = "test_binary_no_compression.bgcode";
constexpr static const char *BINARY_MEATPACK_FILE = "test_binary_meatpack.bgcode";
constexpr static const char *BINARY_HEATSHRINK_FILE = "test_binary_heatshrink.bgcode";
constexpr static const char *BINARY_HEATSHRINK_MEATPACK_FILE = "test_binary_heatshrink_meatpack.bgcode";
// Minimal plaintext gcode carrying a "total toolchanges" metadata line.
constexpr static const char *TOTAL_TOOLCHANGES = "test_total_toolchanges.gcode";
// Minimal plaintext gcode with filament amounts that are not numbers.
constexpr static const char *FILAMENT_NOT_NUMBER = "test_filament_not_number.gcode";
constexpr static const char *NEW_PLAIN = "box_new.gcode";
constexpr static const char *NEW_BINARY = "box_new.bgcode";
// Files with an alternative printer metadata block, with a format unknown to
// the printer. We check we can correctly ignore this block and use the other
// one (INI), no matter if it's after or before that.
constexpr static const char *NEW_BINARY_META_BEFORE = "box_new_meta_before.bgcode";
constexpr static const char *NEW_BINARY_META_AFTER = "box_new_meta_after.bgcode";
constexpr static const char *NEW_ENCRYPTED = "box_new_enc.bgcode";
// Encrypted for multiple printers
// (our being the second)
constexpr static const char *NEW_ENCRYPTED_MULTI = "box_new_enc_multiple.bgcode";
// Encrypted for multiple printers with different algorithms
// (the other printer has an algorithm we don't support)
constexpr static const char *NEW_ENCRYPTED_POLY = "box_new_enc_poly.bgcode";
// Only signed
// That is, encrypted symmetrically by a key that's present in plain in the
// file (mostly just to preserve the format and allow the signing)
constexpr static const char *NEW_SIGNED = "box_new_signed.bgcode";
// Fully encrypted - including metadata and thumbnails.
//
// We currently have no way to utilize these, but we should at least act like a
// gcode without any of these.
constexpr static const char *NEW_ENCRYPTED_FULLY = "box_new_enc_fully.bgcode";

} // namespace
