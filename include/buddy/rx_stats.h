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
    uint32_t ring_peak; ///< EXPERIMENT: highest RX ring usage in bytes (pbuf_rx.cpp), reset by the reporter
};

// Weak, zero-initialised, so every unit (and the host tests, which do not link
// ethernetif.c) shares one instance without a dedicated source file.
__attribute__((weak)) struct buddy_rx_stats buddy_rx_stats;

/// EXPERIMENT: how long the tcpip thread was busy with one thing (ms).
/// kind: enum tcpip_msg_type for messages, 100 = timers, 101 = waiting to
/// re-take the core lock (another task held it). fn: the callback function
/// for callback messages (map with addr2line on the ELF), else 0.
/// Reported per second as the "tcpip_blk" metric (upload_experiment.cpp).
void buddy_tcpip_busy_record(uint32_t ms, uint8_t kind, uint32_t fn);

/// EXPERIMENT: the TCP MSS advertised in new connections (tcp_out.c has a weak
/// TCP_MSS default; upload_experiment.cpp the runtime switch).
uint16_t buddy_advertised_mss(void);

/// EXPERIMENT: per-stage receive latency (upload_experiment.cpp, "net_lat"):
/// kind 0 = ETH interrupt -> network task reading frames (ms, per wake-up),
/// kind 1 = packet posted to the tcpip mailbox -> handled (ms, per packet).
void buddy_net_lat_record(uint8_t kind, uint32_t ms);
/// EXPERIMENT: time of the first ETH RX interrupt not yet serviced (ms, 0 = none);
/// written by the ISR, cleared by the network task.
__attribute__((weak)) volatile uint32_t buddy_eth_irq_ms;

#ifdef __cplusplus
}
#endif
