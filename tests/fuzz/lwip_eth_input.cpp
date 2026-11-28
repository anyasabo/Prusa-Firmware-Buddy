// lwIP receive path from the Ethernet frame up (ethernet_input -> ARP, IPv4 with reassembly, ICMP,
// IGMP, UDP, DHCP with the address conflict detection, and the mDNS responder of lib/WUI/mdns),
// with the options of tests/fuzz/lwip_eth/buddy/lwipopts.h. Any host on the LAN (or on the Wi-Fi)
// can send these frames.
//
// The first input byte selects the address mode: bit 0 set gives a static address (192.168.1.50/24,
// as a printer with a static IP), else DHCP starts. The rest is a list of records:
// [ctl][len lo][len hi][len bytes]. The low 3 bits of ctl select what the harness puts in front:
//   0: nothing (a full Ethernet frame)
//   1: an Ethernet header with the ARP type (the bytes are the ARP packet, of any length)
//   2: an Ethernet header with the IPv4 type (the bytes are the IPv4 packet)
//   3: Ethernet, IPv4 and UDP headers from the DHCP server (port 67) to port 68, and the bytes are
//      a BOOTP message. The harness sets op, xid, chaddr and the magic cookie, so that the DHCP
//      client looks at the options
//   4: Ethernet, IPv4 and UDP headers from a host on the LAN to the mDNS group (port 5353)
//   5 to 7: as 2, but to the broadcast MAC address
// Bits 4 to 6 of ctl advance the time by that count of 500 ms steps after the record, and the
// timers run. At the end, the harness stops DHCP and mDNS, removes the netif, runs the timers for
// 60 s, and clears the timeout list, so that ASan can find a leaked pbuf, pool entry or timeout.

#include <lwip/acd.h>
#include <lwip/dhcp.h>
#include <lwip/etharp.h>
#include <lwip/init.h>
#include <lwip/ip4_frag.h>
#include <lwip/memp.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/prot/dhcp.h>
#include <lwip/prot/ethernet.h>
#include <lwip/prot/iana.h>
#include <lwip/prot/ip4.h>
#include <lwip/prot/udp.h>
#include <lwip/timeouts.h>
#include <netif/ethernet.h>

#include "mdns.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

constexpr size_t MAX_FRAME_LEN = 1514;
constexpr u8_t OUR_MAC[ETH_HWADDR_LEN] = { 0x10, 0x9c, 0x70, 0x00, 0x00, 0x01 };
constexpr u8_t PEER_MAC[ETH_HWADDR_LEN] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x02 };
constexpr u8_t BROADCAST_MAC[ETH_HWADDR_LEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
constexpr u8_t MDNS_MAC[ETH_HWADDR_LEN] = { 0x01, 0x00, 0x5e, 0x00, 0x00, 0xfb };

u32_t now_ms = 0;
u32_t rand_state = 0;
netif nif;
char hostname[] = "prusa-core-one";

err_t linkoutput(netif *, pbuf *) {
    return ERR_OK; // the caller keeps the pbuf
}

err_t netif_init_fn(netif *n) {
    n->hwaddr_len = ETH_HWADDR_LEN;
    std::memcpy(n->hwaddr, OUR_MAC, ETH_HWADDR_LEN);
    n->mtu = 1500;
    n->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP;
    n->name[0] = 'e';
    n->name[1] = 'n';
    n->output = etharp_output;
    n->linkoutput = linkoutput;
    netif_set_hostname(n, hostname);
    return ERR_OK;
}

void run_timers(unsigned steps) {
    for (unsigned i = 0; i < steps; i++) {
        now_ms += 500;
        sys_check_timeouts();
    }
}

void clear_timeouts() {
    sys_timeo **list = sys_timeouts_get_next_timeout();
    while (*list != nullptr) {
        sys_timeo *t = *list;
        *list = t->next;
        memp_free(MEMP_SYS_TIMEOUT, t);
    }
}

u8_t *put_eth(u8_t *at, const u8_t *dst, u16_t type) {
    auto *eth = reinterpret_cast<eth_hdr *>(at);
    std::memcpy(eth->dest.addr, dst, ETH_HWADDR_LEN);
    std::memcpy(eth->src.addr, PEER_MAC, ETH_HWADDR_LEN);
    eth->type = lwip_htons(type);
    return at + SIZEOF_ETH_HDR;
}

u8_t *put_ip_udp(u8_t *at, u32_t src, u32_t dst, u16_t sport, u16_t dport, size_t payload_len) {
    auto *iph = reinterpret_cast<ip_hdr *>(at);
    std::memset(iph, 0, IP_HLEN);
    IPH_VHL_SET(iph, 4, IP_HLEN / 4);
    IPH_LEN_SET(iph, lwip_htons(static_cast<u16_t>(IP_HLEN + UDP_HLEN + payload_len)));
    IPH_TTL_SET(iph, 255);
    IPH_PROTO_SET(iph, IP_PROTO_UDP);
    iph->src.addr = src;
    iph->dest.addr = dst;
    auto *udph = reinterpret_cast<udp_hdr *>(at + IP_HLEN);
    udph->src = lwip_htons(sport);
    udph->dest = lwip_htons(dport);
    udph->len = lwip_htons(static_cast<u16_t>(UDP_HLEN + payload_len));
    udph->chksum = 0; // no UDP checksum
    return at + IP_HLEN + UDP_HLEN;
}

void send_record(u8_t ctl, const u8_t *data, size_t len) {
    u8_t frame[MAX_FRAME_LEN + SIZEOF_ETH_HDR + IP_HLEN + UDP_HLEN];
    u8_t *at = frame;
    switch (ctl & 0x07) {
    case 0:
        break;
    case 1:
        at = put_eth(at, BROADCAST_MAC, ETHTYPE_ARP);
        break;
    case 2:
        at = put_eth(at, OUR_MAC, ETHTYPE_IP);
        break;
    case 3:
        at = put_eth(at, BROADCAST_MAC, ETHTYPE_IP);
        at = put_ip_udp(at, PP_HTONL(LWIP_MAKEU32(192, 168, 1, 1)), PP_HTONL(IPADDR_BROADCAST), LWIP_IANA_PORT_DHCP_SERVER, LWIP_IANA_PORT_DHCP_CLIENT, len);
        break;
    case 4:
        at = put_eth(at, MDNS_MAC, ETHTYPE_IP);
        at = put_ip_udp(at, PP_HTONL(LWIP_MAKEU32(192, 168, 1, 7)), PP_HTONL(LWIP_MAKEU32(224, 0, 0, 251)), LWIP_IANA_PORT_MDNS, LWIP_IANA_PORT_MDNS, len);
        break;
    default:
        at = put_eth(at, BROADCAST_MAC, ETHTYPE_IP);
        break;
    }
    std::memcpy(at, data, len);
    if ((ctl & 0x07) == 3) {
        // Make the client read the options: a reply with our xid and MAC, and the magic cookie.
        // The fuzzer controls the rest, so short messages stay short.
        auto *msg = reinterpret_cast<dhcp_msg *>(at);
        const dhcp *state = netif_dhcp_data(&nif);
        if (len > offsetof(dhcp_msg, op)) {
            msg->op = DHCP_BOOTREPLY;
        }
        if (len >= offsetof(dhcp_msg, xid) + sizeof(msg->xid) && state != nullptr) {
            msg->xid = lwip_htonl(state->xid);
        }
        if (len >= offsetof(dhcp_msg, chaddr) + ETH_HWADDR_LEN) {
            std::memcpy(msg->chaddr, OUR_MAC, ETH_HWADDR_LEN);
        }
        if (len >= offsetof(dhcp_msg, cookie) + sizeof(msg->cookie)) {
            msg->cookie = PP_HTONL(DHCP_MAGIC_COOKIE);
        }
    }
    const size_t frame_len = static_cast<size_t>(at - frame) + len;

    // One pbuf for each frame, as the RX path of the firmware gives.
    pbuf *p = pbuf_alloc(PBUF_RAW, static_cast<u16_t>(frame_len), PBUF_RAM);
    if (p == nullptr) {
        abort();
    }
    std::memcpy(p->payload, frame, frame_len);
    if (nif.input(p, &nif) != ERR_OK) {
        pbuf_free(p);
    }
    run_timers((ctl >> 4) & 0x07);
}

void setup(bool static_ip) {
    now_ms = 0;
    rand_state = 1;
    sys_timeouts_init();
    ip4_addr_t addr, mask, gw;
    if (static_ip) {
        IP4_ADDR(&addr, 192, 168, 1, 50);
        IP4_ADDR(&mask, 255, 255, 255, 0);
        IP4_ADDR(&gw, 192, 168, 1, 1);
    } else {
        ip4_addr_set_zero(&addr);
        ip4_addr_set_zero(&mask);
        ip4_addr_set_zero(&gw);
    }
    if (netif_add(&nif, &addr, &mask, &gw, nullptr, netif_init_fn, ethernet_input) == nullptr) {
        abort();
    }
    netif_set_default(&nif);
    netif_set_up(&nif);
    netif_set_link_up(&nif);
    // As lib/WUI/wui.cpp: the responder with the PrusaLink service.
    mdns_resp_add_netif(&nif);
    mdns_resp_add_service_prusalink(&nif);
    if (!static_ip && dhcp_start(&nif) != ERR_OK) {
        abort();
    }
}

void teardown() {
    mdns_resp_remove_netif(&nif);
    dhcp_release_and_stop(&nif);
    dhcp_cleanup(&nif);
    netif_remove(&nif);
    run_timers(120); // expire the reassembly buffers and the delayed mDNS packets
    clear_timeouts();
}

} // namespace

extern "C" u32_t sys_now(void) {
    return now_ms;
}

extern "C" uint32_t fuzz_lwip_rand(void) {
    rand_state = rand_state * 1103515245u + 12345u;
    return rand_state;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static bool initialized = false;
    if (!initialized) {
        lwip_init();
        clear_timeouts(); // setup() starts them for each input
        mdns_resp_init();
        initialized = true;
    }
    if (size < 1) {
        return 0;
    }
    setup(data[0] & 0x01);
    data++;
    size--;

    while (size >= 3) {
        const u8_t ctl = data[0];
        size_t len = data[1] | (static_cast<size_t>(data[2]) << 8);
        data += 3;
        size -= 3;
        if (len > size) {
            len = size;
        }
        if (len > MAX_FRAME_LEN) {
            len = MAX_FRAME_LEN;
        }
        send_record(ctl, data, len);
        data += len;
        size -= len;
    }

    teardown();
    return 0;
}
