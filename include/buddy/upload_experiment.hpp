#pragma once
// EXPERIMENT (upload-experiments branch, not for upstream): runtime switches for
// the upload fixes, so one flash can A/B them. An upload whose file name contains
// "upx-" sets them for that and later uploads, e.g. "upx-q8-a0-foo.gcode":
//   qN  nhttp upload queue cap (1..17; 8 = stock, 17 = the #5591 fix)
//   a0  stock inbound ooseq caps (512 B / 1 seg per pcb), a1 = fix A (3 KB / 3 segs)
// Defaults at boot: q17 a1. Reported as the "upx" metric when applied.
#include <cstddef>
#include <cstdint>

namespace upload_experiment {
inline size_t queue_cap = 17;
inline uint8_t ooseq_fix_a = 1;
/// Runs on the tcpip thread (nhttp), like the ooseq hooks that read the caps.
void apply_from_path(const char *path);
} // namespace upload_experiment
