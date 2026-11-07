#pragma once
// EXPERIMENT (upload-experiments branch, not for upstream as is): cumulative
// counters for every place an incoming frame or segment can be dropped on the
// upload path. Reported once a second as the "rx_drops" metric (ethernetif.c).
// Plain uint32_t increments: each counter has a single writer task, readers
// only report.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct buddy_rx_stats {
    uint32_t alloc_fail; ///< pbuf_alloc_rx() failed in low_level_input (RX ring full / fragmented)
    uint32_t eth_missed; ///< ETH DMAMFBOCR MFC: frames missed, no free RX descriptor
    uint32_t eth_fifo_ovf; ///< ETH DMAMFBOCR MFA: frames missed by RX FIFO overflow
    uint32_t refused; ///< nhttp TransferSlot::take_pbuf refused (queue full)
    uint32_t refused_drop; ///< lwIP dropped a data segment because refused_data was pending
    uint32_t ooseq_trim; ///< lwIP trimmed the ooseq queue (buddy ooseq caps)
    uint32_t ooseq_reclaim; ///< buddy_ooseq_reclaim() freed something
};

// Weak, zero-initialised, so every unit (and the host tests, which do not link
// ethernetif.c) shares one instance without a dedicated source file.
__attribute__((weak)) struct buddy_rx_stats buddy_rx_stats;

#ifdef __cplusplus
}
#endif
