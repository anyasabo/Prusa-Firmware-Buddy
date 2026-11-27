// lwIP IPv4 + TCP input (ip4_input -> tcp_input) with Buddy's TCP settings and the real
// lib/WUI/ooseq_limit.cpp. Any host on the LAN can send these packets to PrusaLink (port 80).
//
// The input is a list of records: [ctl][len lo][len hi][len bytes of TCP header + payload].
// The harness puts an IPv4 header in front and computes both checksums, so that the packets get
// past the checksum checks. ctl bits:
//   0: run the TCP timers after the packet (retransmission, ooseq timeout, keepalive)
//   1: use the ports of the established connection (else the ports of the record, dst 80 if 0)
//   2: seqno = rcv_nxt of the connection + the low 16 bits of the record's seqno (signed)
//   3: ackno = snd_nxt of the connection - the low 8 bits of the record's ackno
// One listening pcb (port 80) and one established pcb with unacked data exist at the start.
// At the end, all pcbs are removed and a leaked pcb, segment or pool pbuf aborts.
//
// lwIP keeps timer state in statics. To make each input give the same result alone and in a run,
// the ISN is fixed (LWIP_HOOK_TCP_ISN in CMakeLists.txt), tcp_ticks starts at 0, and each input
// ends with an even count of tcp_tmr() calls (tcp_tmr() runs the slow timer on every second call).

#include "tcp_helper.h"

#include <lwip/inet_chksum.h>
#include <lwip/init.h>
#include <lwip/ip4.h>
#include <lwip/priv/tcp_priv.h>
#include <lwip/prot/ip4.h>
#include <lwip/prot/tcp.h>
#include <lwip/stats.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

constexpr u16_t LOCAL_PORT = 80;
constexpr u16_t REMOTE_PORT = 4321;
constexpr size_t MAX_TCP_LEN = 1460;

u32_t now_ms = 0;
unsigned tmr_calls = 0;
netif nif;
test_tcp_txcounters tx;
tcp_pcb *established = nullptr;

err_t recv_fn(void *, tcp_pcb *pcb, pbuf *p, err_t) {
    if (p != nullptr) {
        tcp_recved(pcb, p->tot_len);
        pbuf_free(p);
    }
    return ERR_OK;
}

void established_err(void *, err_t) {
    established = nullptr; // lwIP freed the pcb
}

err_t accept_fn(void *, tcp_pcb *pcb, err_t err) {
    if (err != ERR_OK || pcb == nullptr) {
        return ERR_VAL;
    }
    tcp_recv(pcb, recv_fn);
    return ERR_OK;
}

void setup() {
    helper_check_failures = 0;
    now_ms = 0;
    tmr_calls = 0;
    tcp_ticks = 0;
    test_tcp_init_netif(&nif, &tx, &test_local_ip, &test_netmask);
    nif.input = ip4_input;

    tcp_pcb *listener = tcp_new();
    if (listener == nullptr || tcp_bind(listener, &test_local_ip, LOCAL_PORT) != ERR_OK) {
        abort();
    }
    listener = tcp_listen(listener);
    tcp_accept(listener, accept_fn);

    established = tcp_new();
    if (established == nullptr) {
        abort();
    }
    tcp_arg(established, nullptr);
    tcp_recv(established, recv_fn);
    tcp_err(established, established_err);
    established->snd_wnd = TCP_WND;
    established->snd_wnd_max = TCP_WND;
    established->flags |= TF_SACK; // as if the peer's SYN had SACK-permitted
    tcp_set_state(established, ESTABLISHED, &test_local_ip, &test_remote_ip, LOCAL_PORT, REMOTE_PORT);
    static const char reply[] = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    tcp_write(established, reply, sizeof(reply) - 1, TCP_WRITE_FLAG_COPY);
    tcp_output(established);
}

void send_packet(u8_t ctl, const u8_t *data, size_t len) {
    if (len < TCP_HLEN) {
        return; // tcp_input drops it too; keep the header rewrites below in bounds
    }
    pbuf *p = pbuf_alloc(PBUF_RAW, static_cast<u16_t>(IP_HLEN + len), PBUF_POOL);
    if (p == nullptr || p->next != nullptr) {
        abort(); // PBUF_POOL_BUFSIZE holds one MTU, so the chain has one pbuf
    }
    auto *bytes = static_cast<u8_t *>(p->payload);
    std::memcpy(bytes + IP_HLEN, data, len);
    auto *tcph = reinterpret_cast<tcp_hdr *>(bytes + IP_HLEN);

    if (ctl & 0x02) {
        tcph->src = lwip_htons(REMOTE_PORT);
        tcph->dest = lwip_htons(LOCAL_PORT);
    } else if (tcph->dest == 0) {
        tcph->dest = lwip_htons(LOCAL_PORT);
    }
    if (established != nullptr && (ctl & 0x04)) {
        const auto offset = static_cast<s16_t>(lwip_ntohl(tcph->seqno) & 0xffff);
        tcph->seqno = lwip_htonl(established->rcv_nxt + offset);
    }
    if (established != nullptr && (ctl & 0x08)) {
        tcph->ackno = lwip_htonl(established->snd_nxt - (lwip_ntohl(tcph->ackno) & 0xff));
    }

    auto *iph = reinterpret_cast<ip_hdr *>(bytes);
    std::memset(iph, 0, IP_HLEN);
    IPH_VHL_SET(iph, 4, IP_HLEN / 4);
    IPH_LEN_SET(iph, lwip_htons(p->tot_len));
    IPH_TTL_SET(iph, 64);
    IPH_PROTO_SET(iph, IP_PROTO_TCP);
    iph->src.addr = ip_2_ip4(&test_remote_ip)->addr;
    iph->dest.addr = ip_2_ip4(&test_local_ip)->addr;
    IPH_CHKSUM_SET(iph, inet_chksum(iph, IP_HLEN));

    pbuf_remove_header(p, IP_HLEN);
    tcph->chksum = 0;
    tcph->chksum = ip_chksum_pseudo(p, IP_PROTO_TCP, p->tot_len, &test_remote_ip, &test_local_ip);
    pbuf_add_header(p, IP_HLEN);

    if (nif.input(p, &nif) != ERR_OK) {
        pbuf_free(p);
    }

    if (ctl & 0x01) {
        now_ms += TCP_TMR_INTERVAL;
        tcp_tmr();
        tmr_calls++;
    }
}

} // namespace

extern "C" u32_t sys_now(void) {
    return now_ms;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static bool initialized = false;
    if (!initialized) {
        lwip_init();
        initialized = true;
    }
    setup();

    while (size >= 3) {
        const u8_t ctl = data[0];
        size_t len = data[1] | (static_cast<size_t>(data[2]) << 8);
        data += 3;
        size -= 3;
        if (len > size) {
            len = size;
        }
        if (len > MAX_TCP_LEN) {
            len = MAX_TCP_LEN;
        }
        send_packet(ctl, data, len);
        data += len;
        size -= len;
    }

    tcp_remove_all(); // also checks that no pcb, segment or pool pbuf leaked
    if (tmr_calls % 2 != 0) {
        tcp_tmr(); // no pcbs are left, so this only resets the slow timer phase
    }
    test_tcp_free_tx_packets(&tx);
    netif_list = nullptr;
    established = nullptr;
    if (helper_check_failures != 0) {
        abort();
    }
    return 0;
}
