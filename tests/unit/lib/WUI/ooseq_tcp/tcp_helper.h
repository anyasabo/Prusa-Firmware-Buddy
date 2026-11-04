// Port of lwIP's test/unit/tcp/tcp_helper.h (upstream lwIP master ae6e9edb)
// to Buddy's vendored lwIP 2.1.2 and Catch2. Deviations:
// - check(1) macros (fail_unless/EXPECT*) replaced by helper_check(), which
//   counts failures; the Catch2 test asserts the count is 0.
// - test_tcp_txcounters keeps each transmitted packet as its own pbuf in an
//   array (upstream pbuf_cat()s them into one chain, which loses boundaries).
// - tcp_remove_all() reports leaks through helper_check() instead of failing.
// - Unused tcp_create_rx_segment{,_wnd}() and TEST_{LOCAL,REMOTE}_PORT dropped.
#pragma once

#include "lwip/arch.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned helper_check_failures;
#define helper_check(cond)                                                                       \
    do {                                                                                         \
        if (!(cond)) {                                                                           \
            helper_check_failures++;                                                             \
            fprintf(stderr, "tcp_helper check failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        }                                                                                        \
    } while (0)

#define helper_check_ret(cond, ...)                                                              \
    do {                                                                                         \
        if (!(cond)) {                                                                           \
            helper_check_failures++;                                                             \
            fprintf(stderr, "tcp_helper check failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            return __VA_ARGS__;                                                                  \
        }                                                                                        \
    } while (0)

/* counters used for test_tcp_counters_* callback functions */
struct test_tcp_counters {
    u32_t recv_calls;
    u32_t recved_bytes;
    u32_t recv_calls_after_close;
    u32_t recved_bytes_after_close;
    u32_t close_calls;
    u32_t err_calls;
    err_t last_err;
    char *expected_data;
    u32_t expected_data_len;
};

#define TEST_TCP_MAX_TX_PACKETS 64
struct test_tcp_txcounters {
    u32_t num_tx_calls;
    u32_t num_tx_bytes;
    u8_t copy_tx_packets;
    u32_t num_tx_packets;
    /* each entry: copy of one IP packet, payload at the IP header */
    struct pbuf *tx_packets[TEST_TCP_MAX_TX_PACKETS];
};

extern const ip_addr_t test_local_ip;
extern const ip_addr_t test_remote_ip;
extern const ip_addr_t test_netmask;

void tcp_remove_all(void);
void test_tcp_free_tx_packets(struct test_tcp_txcounters *txcounters);

struct pbuf *tcp_create_segment(ip_addr_t *src_ip, ip_addr_t *dst_ip,
    u16_t src_port, u16_t dst_port, void *data, size_t data_len,
    u32_t seqno, u32_t ackno, u8_t headerflags);
void tcp_set_state(struct tcp_pcb *pcb, enum tcp_state state, const ip_addr_t *local_ip,
    const ip_addr_t *remote_ip, u16_t local_port, u16_t remote_port);
void test_tcp_counters_err(void *arg, err_t err);
err_t test_tcp_counters_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err);
struct tcp_pcb *test_tcp_new_counters_pcb(struct test_tcp_counters *counters);
void test_tcp_input(struct pbuf *p, struct netif *inp);
void test_tcp_init_netif(struct netif *netif, struct test_tcp_txcounters *txcounters,
    const ip_addr_t *ip_addr, const ip_addr_t *netmask);

#ifdef __cplusplus
}
#endif
