#pragma once
// EXPERIMENT (branch upload-switches, not for upstream): cumulative counters for
// each place where the upload path can drop a received frame or segment.
// ethernetif.c reports them once a second as the metrics "rxd_eth" and "rxd_tcp".
// Plain uint32_t increments: each counter has one writer task. Readers only report.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// How to read them: refused counts each refusal, and lwIP offers the same pbuf
// again (at each new segment and every 250 ms), so one pbuf can count many
// times. refused_drop and ooseq_trim count all TCP connections, also Connect.
// ooseq_trim counts trim events, not segments.
struct buddy_rx_stats {
    uint32_t alloc_fail; ///< pbuf_alloc_rx() failed in low_level_input (RX ring full)
    uint32_t eth_missed; ///< ETH DMAMFBOCR MFC: frames missed, no free RX descriptor
    uint32_t eth_fifo_ovf; ///< ETH DMAMFBOCR MFA: frames missed by RX FIFO overflow
    uint32_t refused; ///< nhttp TransferSlot::take_pbuf refused a pbuf (queue full)
    uint32_t refused_drop; ///< lwIP dropped a data segment because refused_data was pending
    uint32_t ooseq_trim; ///< lwIP trimmed the out-of-order queue (buddy ooseq caps)
};

// Weak and zero-initialised, so that each unit (and the host tests, which do
// not link ethernetif.c) shares one instance without its own source file.
__attribute__((weak)) struct buddy_rx_stats buddy_rx_stats;

#ifdef __cplusplus
}
#endif
