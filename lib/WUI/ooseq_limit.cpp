#include "ooseq_limit.hpp"

#include <lwip/tcp.h>
#include <lwip/priv/tcp_priv.h>
#ifndef OOSEQ_LIMIT_OVERRIDABLE
    #include <lwip/tcpip.h>
#endif

#include <atomic>

// Intention behind the limits.
//
// We don't want to get into the situation where all our receive buffers are
// occuppied by out-of-sequence packets. That way an in-sequence packet can
// never come, so it can't unblock the out of sequence ones either - and we
// soft-deadlock the whole receive end (until something actually times out and
// frees stuff - which is easy to prevent by an attacker).
//
// Therefore, we want to somehow limit the amount of out of sequence data we
// allow and start dropping them earlier than we run out of space. But we also
// want to support some out of sequence handling - the Connect connection is
// over the wide Internet (so the out of sequence tends to happen on it more),
// has higher lattency (slower recovery) and sensitive for speed (during GCode
// downloads).
//
// Therefore, we split the connections to two groups:
//
// * Outbount TCP connections (we dial out - the Connect case, single connection).
// * Inbound connections (PrusaLink, multiple connection, possibly created by
//   an attacker).
//
// We give significantly more room to the Connect case.
//
// Note that nothing from here is run or consulted for in-sequence packets at
// all.

namespace {

// Well-known/server ports (0-1023, IANA range); our listeners (PrusaLink
// HTTP on 80, future HTTPS, ...) live here, while outbound connections
// (Connect, downloads) get an ephemeral local_port assigned by lwIP.
constexpr uint16_t WELL_KNOWN_PORT_MAX = 1024;

bool is_inbound(const struct tcp_pcb *pcb) {
    return pcb->local_port < WELL_KNOWN_PORT_MAX;
}

struct OoseqUsage {
    uint32_t all_bytes = 0;
    uint32_t inbound_bytes = 0;
    uint16_t all_segs = 0;
    uint16_t inbound_segs = 0;
};

// Sums ooseq usage of every active pcb except `exclude`. Called from
// tcp_input() with the lwIP core lock held, so tcp_active_pcbs and the
// per-pcb ooseq chains are stable for the duration of the walk.
OoseqUsage other_pcbs_usage(const struct tcp_pcb *exclude) {
    OoseqUsage usage;
    for (const struct tcp_pcb *pcb = tcp_active_pcbs; pcb != nullptr; pcb = pcb->next) {
        if (pcb == exclude) {
            continue;
        }
        const bool inbound = is_inbound(pcb);
        for (const struct tcp_seg *seg = pcb->ooseq; seg != nullptr; seg = seg->next) {
            const uint32_t bytes = seg->p->tot_len;
            usage.all_bytes += bytes;
            usage.all_segs++;
            if (inbound) {
                usage.inbound_bytes += bytes;
                usage.inbound_segs++;
            }
        }
    }
    return usage;
}

#ifdef OOSEQ_LIMIT_OVERRIDABLE
// Host tests only: lets a test run the real lwIP input path with other caps.
ooseq_limit::Limits active_limits = ooseq_limit::DEFAULT_LIMITS;
uint32_t hook_calls = 0;
    #define COUNT_HOOK_CALL() (++hook_calls)
#else
    #define COUNT_HOOK_CALL() ((void)0)
constexpr const ooseq_limit::Limits &active_limits = ooseq_limit::DEFAULT_LIMITS;
#endif

// Set while a reclaim is queued on the tcpip thread, so a burst of allocation
// failures queues it once.
std::atomic<bool> reclaim_pending = false;

void reclaim_callback(void *) {
    reclaim_pending = false;
    buddy_ooseq_reclaim();
}

#ifdef OOSEQ_LIMIT_OVERRIDABLE
ooseq_limit::ReclaimPoster reclaim_poster = nullptr;
err_t post_reclaim() {
    return reclaim_poster ? reclaim_poster(reclaim_callback) : ERR_IF;
}
#else
err_t post_reclaim() {
    return tcpip_try_callback(reclaim_callback, nullptr);
}
#endif

} // namespace

#ifdef OOSEQ_LIMIT_OVERRIDABLE
namespace ooseq_limit {
void set_reclaim_poster_for_test(ReclaimPoster poster) {
    reclaim_poster = poster;
    reclaim_pending = false;
}
void set_limits_for_test(const Limits &limits) {
    active_limits = limits;
}
uint32_t hook_calls_for_test() {
    return hook_calls;
}
} // namespace ooseq_limit
#endif

extern "C" {

uint32_t buddy_ooseq_bytes_limit(const struct tcp_pcb *pcb) {
    COUNT_HOOK_CALL();
    const OoseqUsage usage = other_pcbs_usage(pcb);
    return ooseq_limit::compute_bytes_limit(is_inbound(pcb), usage.all_bytes, usage.inbound_bytes, active_limits);
}

uint16_t buddy_ooseq_pbufs_limit(const struct tcp_pcb *pcb) {
    COUNT_HOOK_CALL();
    const OoseqUsage usage = other_pcbs_usage(pcb);
    return ooseq_limit::compute_segs_limit(is_inbound(pcb), usage.all_segs, usage.inbound_segs, active_limits);
}

// Fix C: give out-of-order data back when the RX ring runs dry.
//
// Must run on the tcpip thread (core lock held); buddy_ooseq_reclaim_request()
// schedules it there. Frees the ooseq queues of inbound pcbs first and
// only touches outbound (Connect) pcbs if no inbound pcb held anything.
// tcp_free_ooseq() also clears the pcb's SACK list (lwIP 2.1.2, tcp.c), so the
// next ACK stops advertising data we no longer hold.
//
// Returns the number of payload bytes released.
uint32_t buddy_ooseq_reclaim(void) {
    uint32_t freed = 0;
    for (int pass = 0; pass < 2 && freed == 0; pass++) {
        const bool want_inbound = pass == 0;
        for (struct tcp_pcb *pcb = tcp_active_pcbs; pcb != nullptr; pcb = pcb->next) {
            if (is_inbound(pcb) != want_inbound || pcb->ooseq == nullptr) {
                continue;
            }
            for (const struct tcp_seg *seg = pcb->ooseq; seg != nullptr; seg = seg->next) {
                freed += seg->p->tot_len;
            }
            tcp_free_ooseq(pcb);
        }
    }
    return freed;
}

// Called by pbuf_alloc_rx() when the RX ring is full, from the network or ESP
// task (not from an ISR). Queues buddy_ooseq_reclaim() on the tcpip thread,
// once until it has run; if the tcpip mailbox is full, the next failure retries.
void buddy_ooseq_reclaim_request(void) {
    if (reclaim_pending.exchange(true)) {
        return;
    }
    if (post_reclaim() != ERR_OK) {
        reclaim_pending = false;
    }
}

} // extern "C"
