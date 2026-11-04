// lwIP options for the ooseq_tcp host test. Lives at buddy/lwipopts.h because
// the vendored lwip/opt.h includes <buddy/lwipopts.h>; the test dir is the
// only include path that provides it in this target.
//
// Not include/buddy/lwipopts.h: that one drags in printers.h, FreeRTOS and
// option headers. Instead this copies every option from it that changes how
// received TCP data is handled (marked "Buddy"), and picks simple host values
// for the rest (NO_SYS, malloc-backed heap, a real PBUF_POOL for the test
// helpers, stats on).
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Host harness -----------------------------------------------------------
#define NO_SYS               1
#define LWIP_TIMERS          1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_NETCONN         0
#define LWIP_SOCKET          0
#define LWIP_IPV4            1
#define LWIP_IPV6            0
#define LWIP_UDP             0
#define LWIP_RAW             0
#define LWIP_DHCP            0
#define LWIP_DNS             0
#define LWIP_IGMP            0
#define LWIP_ICMP            0
#define LWIP_ARP             0
#define LWIP_ETHERNET        0
#define LWIP_ALTCP           0
#define IP_REASSEMBLY        0
#define IP_FRAG              0
#define MEM_LIBC_MALLOC      1
#define MEM_ALIGNMENT        8
#define MEMP_NUM_TCP_SEG     16 // Buddy uses the lwIP default
#define PBUF_POOL_SIZE       64
// One frame per pbuf, like Buddy's pbuf_alloc_rx ring: pbuf_clen() of a
// received segment is 1, which matters for TCP_OOSEQ_PBUFS_LIMIT.
#define PBUF_POOL_BUFSIZE  1600
#define LWIP_STATS         1
#define TCP_STATS          1
#define MEMP_STATS         1
#define LWIP_STATS_DISPLAY 0

// --- Buddy (include/buddy/lwipopts.h) ---------------------------------------
#define TCP_MSS                        1024
#define TCP_WND                        (8 * TCP_MSS)
#define TCP_SND_BUF                    (2 * TCP_MSS)
#define LWIP_WND_SCALE                 0
#define TCP_RCV_SCALE                  0
#define LWIP_DISABLE_TCP_SANITY_CHECKS 1
#define TCP_OVERSIZE                   128
#define LWIP_TCP_SACK_OUT              1
#define TCP_QUEUE_OOSEQ                1 // lwIP default, stated for clarity
#define CHECKSUM_GEN_IP                1
#define CHECKSUM_GEN_TCP               1
#define CHECKSUM_CHECK_IP              1
#define CHECKSUM_CHECK_TCP             1
#define LWIP_CHECKSUM_ON_COPY          1
#define MEMP_NUM_TCP_PCB               12

// Direction-aware, globally-bounded ooseq caps; see lib/WUI/ooseq_limit.cpp.
// Verbatim from include/buddy/lwipopts.h:124-129.
struct tcp_pcb;
uint32_t buddy_ooseq_bytes_limit(const struct tcp_pcb *pcb);
uint16_t buddy_ooseq_pbufs_limit(const struct tcp_pcb *pcb);
#define TCP_OOSEQ_BYTES_LIMIT(pcb) buddy_ooseq_bytes_limit(pcb)
#define TCP_OOSEQ_PBUFS_LIMIT(pcb) buddy_ooseq_pbufs_limit(pcb)

#ifdef __cplusplus
}
#endif
