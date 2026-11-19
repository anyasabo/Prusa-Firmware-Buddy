#pragma once
// EXPERIMENT (branch upload-switches, not for upstream): runtime switches for
// the three upload fixes of PR #5598, so that one flash can compare them.
//
// An upload whose file name contains "upx-" sets the switches that follow it,
// for that upload and the uploads after it (until the next "upx-" upload or a
// reboot). Example: "upx-r4-a0-q8-test.gcode" is the stock behavior.
//
//   rN  Ethernet RX descriptors in the DMA ring, 2..ETH_RXBUFNB (stock 4, fix 10).
//       The network task applies a change when the ring is empty, within 500 ms,
//       and only while the Ethernet link is up. It has no effect on Wi-Fi.
//       The heap does not change: the arrays keep ETH_RXBUFNB entries.
//   aN  a0 = stock inbound out-of-order caps (512 B / 1 segment for each
//       connection, 1024 B / 2 segments for all inbound connections),
//       a1 = the fix (3072 B / 3 segments).
//   qN  nhttp upload queue cap, 1..17 (stock 8, fix 17).
//
// Defaults at boot: r10 a1 q17 (the fixes). Parsing stops at the first key
// that is not r, a or q, or at a key without digits after it. A value out of
// range is clamped. An upload that the printer refuses later (for example 409,
// another transfer runs) still sets the switches.
//
// Use a small upload to change the switches, then do the measured uploads.
// Else the switch change falls in the measured upload.
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace upload_switches {

inline constexpr uint8_t RX_DESC_MIN = 2;
inline constexpr uint8_t RX_DESC_MAX = 10; // ETH_RXBUFNB of the non-MINI boards
inline constexpr uint8_t QUEUE_MIN = 1;
inline constexpr uint8_t QUEUE_MAX = 17; // nhttp PbufQueueMax

struct Values {
    uint8_t rx_desc = RX_DESC_MAX;
    uint8_t ooseq_fix = 1;
    uint8_t queue_cap = QUEUE_MAX;
};

inline uint8_t clamp(long v, uint8_t lo, uint8_t hi) {
    return v < lo ? lo : (v > hi ? hi : static_cast<uint8_t>(v));
}

/// Applies the switches in `path` to `values`. Returns false (and leaves
/// `values` unchanged) if `path` has no "upx-".
inline bool parse(const char *path, Values &values) {
    const char *p = path ? strstr(path, "upx-") : nullptr;
    if (p == nullptr) {
        return false;
    }
    p += 4;
    while (*p == 'r' || *p == 'a' || *p == 'q') {
        const char key = *p++;
        if (!isdigit(static_cast<unsigned char>(*p))) {
            break; // a key without digits (also a sign or a space): stop
        }
        char *end = nullptr;
        const long v = strtol(p, &end, 10);
        p = end;
        switch (key) {
        case 'r':
            values.rx_desc = clamp(v, RX_DESC_MIN, RX_DESC_MAX);
            break;
        case 'a':
            values.ooseq_fix = v ? 1 : 0;
            break;
        case 'q':
            values.queue_cap = clamp(v, QUEUE_MIN, QUEUE_MAX);
            break;
        }
        if (*p != '-') {
            break;
        }
        p++;
    }
    return true;
}

} // namespace upload_switches
