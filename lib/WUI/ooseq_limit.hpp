#pragma once

#include <algorithm>
#include <cstdint>

/// Pure arithmetic behind the direction-aware, globally-bounded TCP
/// out-of-order queue caps (see TCP_OOSEQ_BYTES_LIMIT / TCP_OOSEQ_PBUFS_LIMIT
/// in lwipopts.h and ooseq_limit.cpp). Kept free of lwIP types (inline, no
/// lwIP includes) so it can be exercised by a host unit test.
namespace ooseq_limit {

/// The full set of caps. The defaults are the shipped values; tests (and
/// experiments) can evaluate other sets through the `limits` parameter of the
/// compute_* functions below.
struct Limits {
    // Ring bytes budget (protects the shared RX pbuf ring, pbuf_rx.cpp). Worst-case
    // pinned ring ~= global_bytes + global_segs * per-frame overhead; keep it well
    // under the ring size in pbuf_rx.cpp.
    uint32_t global_bytes = 4096;
    uint32_t inbound_share_bytes = 3072;
    uint32_t inbound_pcb_bytes = 3072;
    uint32_t outbound_pcb_bytes = 4096;

    // tcp_seg count budget (protects the shared MEMP_NUM_TCP_SEG pool).
    uint16_t global_segs = 5;
    uint16_t inbound_share_segs = 3;
    uint16_t inbound_pcb_segs = 3;
    uint16_t outbound_pcb_segs = 4;
};

inline constexpr Limits DEFAULT_LIMITS {};

inline constexpr uint32_t GLOBAL_BYTES = DEFAULT_LIMITS.global_bytes;
inline constexpr uint32_t INBOUND_SHARE_BYTES = DEFAULT_LIMITS.inbound_share_bytes;
inline constexpr uint32_t INBOUND_PCB_BYTES = DEFAULT_LIMITS.inbound_pcb_bytes;
inline constexpr uint32_t OUTBOUND_PCB_BYTES = DEFAULT_LIMITS.outbound_pcb_bytes;

inline constexpr uint16_t GLOBAL_SEGS = DEFAULT_LIMITS.global_segs;
inline constexpr uint16_t INBOUND_SHARE_SEGS = DEFAULT_LIMITS.inbound_share_segs;
inline constexpr uint16_t INBOUND_PCB_SEGS = DEFAULT_LIMITS.inbound_pcb_segs;
inline constexpr uint16_t OUTBOUND_PCB_SEGS = DEFAULT_LIMITS.outbound_pcb_segs;

namespace detail {
    template <typename T>
    inline T room(T budget, T used) {
        return used < budget ? budget - used : 0;
    }
} // namespace detail

/// Max ooseq payload bytes a pcb of the given class may keep, given the
/// ooseq bytes already held by every other pcb.
inline uint32_t compute_bytes_limit(bool is_inbound, uint32_t other_all_bytes, uint32_t other_inbound_bytes, const Limits &limits = DEFAULT_LIMITS) {
    const uint32_t global_room = detail::room(limits.global_bytes, other_all_bytes);
    if (is_inbound) {
        const uint32_t inbound_room = detail::room(limits.inbound_share_bytes, other_inbound_bytes);
        return std::min({ limits.inbound_pcb_bytes, inbound_room, global_room });
    } else {
        return std::min(limits.outbound_pcb_bytes, global_room);
    }
}

/// Max ooseq segments a pcb of the given class may keep, given the ooseq
/// segments already held by every other pcb.
inline uint16_t compute_segs_limit(bool is_inbound, uint16_t other_all_segs, uint16_t other_inbound_segs, const Limits &limits = DEFAULT_LIMITS) {
    const uint16_t global_room = detail::room(limits.global_segs, other_all_segs);
    if (is_inbound) {
        const uint16_t inbound_room = detail::room(limits.inbound_share_segs, other_inbound_segs);
        return std::min({ limits.inbound_pcb_segs, inbound_room, global_room });
    } else {
        return std::min(limits.outbound_pcb_segs, global_room);
    }
}

#ifdef OOSEQ_LIMIT_OVERRIDABLE
/// Host tests only (see ooseq_limit.cpp).
void set_limits_for_test(const Limits &limits);
/// Stands in for tcpip_try_callback() (the host test has no tcpip thread).
/// Setting it also clears the pending flag.
using ReclaimPoster = int8_t (*)(void (*callback)(void *));
void set_reclaim_poster_for_test(ReclaimPoster poster);
/// Number of times lwIP consulted the hooks (bytes + pbufs), for sanity checks.
uint32_t hook_calls_for_test();
#endif

} // namespace ooseq_limit

/// Fix C; see ooseq_limit.cpp. tcpip thread only.
extern "C" uint32_t buddy_ooseq_reclaim(void);
/// Schedules buddy_ooseq_reclaim() on the tcpip thread. Any task, not ISRs.
extern "C" void buddy_ooseq_reclaim_request(void);
