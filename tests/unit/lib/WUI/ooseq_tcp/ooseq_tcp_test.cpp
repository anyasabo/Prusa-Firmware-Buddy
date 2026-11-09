// Runs Buddy's vendored lwIP 2.1.2 TCP input path with Buddy's TCP settings
// and the real lib/WUI/ooseq_limit.cpp, to show what the out-of-order (ooseq)
// caps do to a PrusaLink-style upload (inbound connection, local port 80).
//
// "Segment k" is the k-th MSS-sized payload of the stream, at sequence offset
// (k - 1) * segment size from the connection's initial rcv_nxt. Every scenario
// delivers segment 1, "loses" segment 2, delivers 3, 4, 5 out of order, and
// then retransmits 2.
//
// The connections are created established with tcp_set_state() (no
// handshake), so SACK is not negotiated; TF_SACK is set by hand, as if the
// peer's SYN carried SACK-permitted (macOS, Linux and Windows all send it).

#include <ooseq_limit.hpp>

#include "tcp_helper.h"

#include <lwip/init.h>
#include <lwip/priv/tcp_priv.h>
#include <lwip/prot/ip4.h>
#include <lwip/prot/tcp.h>
#include <lwip/stats.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

extern "C" u32_t sys_now(void) {
    return 0;
}

namespace {

using ooseq_limit::Limits;

constexpr u16_t HTTP_PORT = 80; // PrusaLink; < 1024, so "inbound"
constexpr u16_t HIGH_PORT = 8080; // >= 1024, so treated like an outbound (Connect) pcb

/// Fix A as proposed in draft 03: room for 3 MSS-sized segments on one
/// inbound pcb, the inbound share sized to match, global caps unchanged.
constexpr Limits FIX_A = [] {
    Limits l;
    l.inbound_pcb_bytes = 3 * 1024;
    l.inbound_pcb_segs = 3;
    l.inbound_share_bytes = 3 * 1024;
    l.inbound_share_segs = 3;
    return l;
}();

using SackBlock = std::pair<u32_t, u32_t>; // [left, right), host order

struct TxPacket {
    u32_t ackno;
    std::vector<SackBlock> sacks;
};

TxPacket parse_tx(const pbuf *p) {
    REQUIRE(p->len == p->tot_len); // PBUF_RAM copy, contiguous
    const auto *bytes = static_cast<const u8_t *>(p->payload);
    const auto *iph = reinterpret_cast<const ip_hdr *>(bytes);
    const size_t ip_len = IPH_HL_BYTES(iph);
    REQUIRE(IPH_PROTO(iph) == IP_PROTO_TCP);
    const auto *tcph = reinterpret_cast<const tcp_hdr *>(bytes + ip_len);
    const size_t tcp_len = TCPH_HDRLEN_BYTES(tcph);
    TxPacket out { lwip_ntohl(tcph->ackno), {} };
    const u8_t *opt = bytes + ip_len + TCP_HLEN;
    const u8_t *end = bytes + ip_len + tcp_len;
    while (opt < end) {
        const u8_t kind = opt[0];
        if (kind == 0) { // end of options
            break;
        }
        if (kind == 1) { // NOP
            opt++;
            continue;
        }
        REQUIRE(opt + 1 < end);
        const u8_t len = opt[1];
        REQUIRE(len >= 2);
        if (kind == 5) { // SACK
            for (size_t i = 2; i + 8 <= len; i += 8) {
                u32_t l, r;
                memcpy(&l, opt + i, 4);
                memcpy(&r, opt + i + 4, 4);
                out.sacks.emplace_back(lwip_ntohl(l), lwip_ntohl(r));
            }
        }
        opt += len;
    }
    return out;
}

struct Conn {
    test_tcp_counters counters {};
    tcp_pcb *pcb = nullptr;
    u32_t base = 0; // rcv_nxt before segment 1
    u16_t seg_len = 1024;

    u32_t seg_start(int k) const { return base + static_cast<u32_t>(k - 1) * seg_len; }
    u32_t seg_end(int k) const { return seg_start(k) + seg_len; }

    size_t ooseq_segs() const {
        size_t n = 0;
        for (const tcp_seg *s = pcb->ooseq; s; s = s->next) {
            n++;
        }
        return n;
    }
    u32_t ooseq_bytes() const {
        u32_t n = 0;
        for (const tcp_seg *s = pcb->ooseq; s; s = s->next) {
            n += s->p->tot_len;
        }
        return n;
    }
    bool has_sacks() const {
        for (const auto &s : pcb->rcv_sacks) {
            if (s.left != s.right) {
                return true;
            }
        }
        return false;
    }
};

struct Fixture {
    netif nif {};
    test_tcp_txcounters tx {};
    std::array<u8_t, 1024> data {};
    u16_t next_remote_port = 50000;

    explicit Fixture(const Limits &limits = ooseq_limit::DEFAULT_LIMITS) {
        static bool initialized = false;
        if (!initialized) {
            lwip_init();
            initialized = true;
        }
        ooseq_limit::set_limits_for_test(limits);
        helper_check_failures = 0;
        test_tcp_init_netif(&nif, &tx, &test_local_ip, &test_netmask);
        tx.copy_tx_packets = 1;
    }

    ~Fixture() {
        tcp_remove_all(); // also checks that no pcb / seg / pool pbuf leaked
        test_tcp_free_tx_packets(&tx);
        netif_list = nullptr; // `nif` lives on this fixture's stack frame
        ooseq_limit::set_limits_for_test(ooseq_limit::DEFAULT_LIMITS);
        CHECK(helper_check_failures == 0);
    }

    /// Conns live in a deque so the counters pointer given to tcp_arg() stays valid.
    std::deque<Conn> conns;

    Conn &open(u16_t local_port, u16_t seg_len = 1024) {
        Conn &c = conns.emplace_back();
        c.seg_len = seg_len;
        c.pcb = test_tcp_new_counters_pcb(&c.counters);
        REQUIRE(c.pcb != nullptr);
        tcp_set_state(c.pcb, ESTABLISHED, &test_local_ip, &test_remote_ip, local_port, next_remote_port++);
        // Stands in for the peer's SACK-permitted option in the SYN.
        tcp_set_flags(c.pcb, TF_SACK);
        c.base = c.pcb->rcv_nxt;
        return c;
    }

    /// Feed segment k of `c` to tcp_input(); returns the packets lwIP sent in response.
    std::vector<TxPacket> inject(Conn &c, int k) {
        test_tcp_free_tx_packets(&tx);
        // Payload content is irrelevant (counters.expected_data stays NULL);
        // only lengths and sequence numbers are checked.
        pbuf *p = tcp_create_segment(&c.pcb->remote_ip, &c.pcb->local_ip, c.pcb->remote_port, c.pcb->local_port,
            data.data(), c.seg_len, c.seg_start(k), c.pcb->lastack, TCP_ACK);
        REQUIRE(p != nullptr);
        test_tcp_input(p, &nif);
        std::vector<TxPacket> out;
        for (u32_t i = 0; i < tx.num_tx_packets; i++) {
            out.push_back(parse_tx(tx.tx_packets[i]));
        }
        return out;
    }

    /// Force an ACK out of `c` and return it.
    TxPacket ack_now(Conn &c) {
        test_tcp_free_tx_packets(&tx);
        tcp_ack_now(c.pcb);
        REQUIRE(tcp_output(c.pcb) == ERR_OK);
        REQUIRE(tx.num_tx_packets == 1);
        return parse_tx(tx.tx_packets[0]);
    }
};

/// Segment 1 in order, segment 2 lost, segments 3..5 out of order. Returns
/// the ACKs sent for 3..5.
std::vector<TxPacket> lose_segment_2(Fixture &f, Conn &c) {
    const auto first = f.inject(c, 1);
    REQUIRE(c.counters.recved_bytes == c.seg_len);
    REQUIRE(c.pcb->rcv_nxt == c.seg_end(1));
    (void)first; // segment 1 is acked lazily (delayed ACK); nothing to check
    std::vector<TxPacket> acks;
    for (int k = 3; k <= 5; k++) {
        auto sent = f.inject(c, k);
        // An out-of-order segment is always acked immediately (dup ACK).
        REQUIRE(sent.size() == 1);
        CHECK(sent[0].ackno == c.seg_end(1));
        acks.push_back(sent[0]);
    }
    return acks;
}

bool any_sack(const std::vector<TxPacket> &acks) {
    for (const auto &a : acks) {
        if (!a.sacks.empty()) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("ooseq_tcp sanity: lwIP consults Buddy's ooseq hooks", "[ooseq]") {
    Fixture f;
    Conn &c = f.open(HTTP_PORT);
    const uint32_t before = ooseq_limit::hook_calls_for_test();
    f.inject(c, 1);
    CHECK(ooseq_limit::hook_calls_for_test() == before); // in-order data never asks
    f.inject(c, 3);
    CHECK(ooseq_limit::hook_calls_for_test() == before + 2); // bytes + pbufs hook
}

TEST_CASE("ooseq_tcp T1: stock limits drop every out-of-order MSS segment on port 80 (bug 3)", "[ooseq]") {
    Fixture f;
    Conn &c = f.open(HTTP_PORT);
    const auto acks = lose_segment_2(f, c);

    CHECK(c.pcb->ooseq == nullptr);
    CHECK_FALSE(c.has_sacks());
    CHECK_FALSE(any_sack(acks)); // the sender learns nothing about 3..5

    // Retransmitting 2 delivers only 2; 3..5 must be sent again (go-back-N).
    f.inject(c, 2);
    CHECK(c.counters.recved_bytes == 2 * 1024);
    CHECK(c.pcb->rcv_nxt == c.seg_end(2));
}

TEST_CASE("ooseq_tcp T2: stock limits on a high port (outbound-style) keep and SACK the segments", "[ooseq]") {
    Fixture f;
    Conn &c = f.open(HIGH_PORT);
    const auto acks = lose_segment_2(f, c);

    CHECK(c.ooseq_segs() == 3);
    CHECK(c.ooseq_bytes() == 3 * 1024);
    REQUIRE_FALSE(acks.back().sacks.empty());
    CHECK(acks.back().sacks.front() == SackBlock { c.seg_start(3), c.seg_end(5) });

    f.inject(c, 2);
    CHECK(c.counters.recved_bytes == 5 * 1024); // 1, then 2..5 in one go
    CHECK(c.pcb->rcv_nxt == c.seg_end(5));
    CHECK(c.pcb->ooseq == nullptr);
}

namespace {
/// What a working out-of-order queue looks like on port 80: 3..5 kept and
/// SACKed, and retransmitting 2 delivers 2..5 at once.
void expect_ooseq_works_on_port_80(const Limits &limits) {
    Fixture f(limits);
    Conn &c = f.open(HTTP_PORT);
    const auto acks = lose_segment_2(f, c);

    CHECK(c.ooseq_segs() == 3);
    CHECK(c.ooseq_bytes() == 3 * 1024);
    // Each dup ACK grows the SACK block: [3,4) -> [3,5) -> [3,6).
    for (int i = 0; i < 3; i++) {
        CHECK(acks[i].sacks.size() == 1);
        if (acks[i].sacks.size() == 1) {
            CHECK(acks[i].sacks[0] == SackBlock { c.seg_start(3), c.seg_end(3 + i) });
        }
    }

    f.inject(c, 2);
    CHECK(c.counters.recved_bytes == 5 * 1024);
    CHECK(c.pcb->rcv_nxt == c.seg_end(5));
    CHECK(c.pcb->ooseq == nullptr);
}
} // namespace

TEST_CASE("ooseq_tcp T3: fix A keeps and SACKs the segments on port 80", "[ooseq]") {
    expect_ooseq_works_on_port_80(FIX_A);
}

// T1 again, phrased the other way round: the "fixed" expectations must fail
// on the shipped limits. [!shouldfail] makes ctest go red if they ever pass,
// i.e. if this test stops reproducing bug 3.
TEST_CASE("ooseq_tcp T1-inverse: stock limits fail the fixed expectations", "[ooseq][!shouldfail]") {
    expect_ooseq_works_on_port_80(ooseq_limit::DEFAULT_LIMITS);
}

TEST_CASE("ooseq_tcp T3b: fix A trims a 4th out-of-order segment and its SACK", "[ooseq]") {
    Fixture f(FIX_A);
    Conn &c = f.open(HTTP_PORT);
    lose_segment_2(f, c);
    const auto sent = f.inject(c, 6);
    REQUIRE(sent.size() == 1);
    CHECK(c.ooseq_segs() == 3);
    REQUIRE(sent[0].sacks.size() == 1);
    CHECK(sent[0].sacks[0] == SackBlock { c.seg_start(3), c.seg_end(5) }); // 6 not advertised
}

TEST_CASE("ooseq_tcp T4: with fix A the inbound share and global caps still bound the total (PFB-012)", "[ooseq]") {
    Fixture f(FIX_A);
    Conn &a = f.open(HTTP_PORT);
    Conn &b = f.open(HTTP_PORT);
    Conn &outbound = f.open(HIGH_PORT);

    lose_segment_2(f, a);
    CHECK(a.ooseq_bytes() == FIX_A.inbound_share_bytes);

    // A second inbound connection (another browser tab, or an attacker) gets
    // nothing: the inbound share is used up by `a`.
    const auto b_acks = lose_segment_2(f, b);
    CHECK(b.pcb->ooseq == nullptr);
    CHECK_FALSE(any_sack(b_acks));

    // The outbound (Connect) pcb still gets the global remainder:
    // 4096 - 3072 = 1024 B and 5 - 3 = 2 segs -> one 1024 B segment.
    lose_segment_2(f, outbound);
    CHECK(outbound.ooseq_bytes() == FIX_A.global_bytes - FIX_A.inbound_share_bytes);
    CHECK(outbound.ooseq_segs() == 1);

    const u32_t total_bytes = a.ooseq_bytes() + b.ooseq_bytes() + outbound.ooseq_bytes();
    const size_t total_segs = a.ooseq_segs() + b.ooseq_segs() + outbound.ooseq_segs();
    CHECK(total_bytes <= FIX_A.global_bytes);
    CHECK(total_segs <= FIX_A.global_segs);
    CHECK(MEMP_STATS_GET(used, MEMP_TCP_SEG) == total_segs);
}

TEST_CASE("ooseq_tcp T4b: stock limits, the same outbound pcb gets 3 segments", "[ooseq]") {
    // For comparison with T4: while an inbound upload holds its full share,
    // fix A cuts what Connect is guaranteed from 3072 B / 3 segs to 1024 B /
    // 2 segs (here: 3 MSS segments kept -> 1).
    Fixture f;
    Conn &a = f.open(HTTP_PORT);
    Conn &outbound = f.open(HIGH_PORT);
    lose_segment_2(f, a);
    lose_segment_2(f, outbound);
    CHECK(a.pcb->ooseq == nullptr);
    CHECK(outbound.ooseq_segs() == 3);
}

TEST_CASE("ooseq_tcp T5: stock limits with 512 B segments keep exactly one (macOS MSS-512 case)", "[ooseq]") {
    Fixture f;
    Conn &c = f.open(HTTP_PORT, 512);
    const auto acks = lose_segment_2(f, c);

    REQUIRE(c.ooseq_segs() == 1); // 512 is not > 512; a 2nd seg exceeds both inbound caps
    CHECK(c.pcb->ooseq->tcphdr->seqno == c.seg_start(3));
    for (const auto &a : acks) {
        REQUIRE(a.sacks.size() == 1);
        CHECK(a.sacks[0] == SackBlock { c.seg_start(3), c.seg_end(3) });
    }

    f.inject(c, 2);
    CHECK(c.counters.recved_bytes == 3 * 512);
    CHECK(c.pcb->rcv_nxt == c.seg_end(3));
}

TEST_CASE("ooseq_tcp T6: fix C reclaim frees inbound ooseq first and clears SACKs", "[ooseq]") {
    Fixture f(FIX_A);
    Conn &in = f.open(HTTP_PORT);
    Conn &out = f.open(HIGH_PORT);
    lose_segment_2(f, in);
    lose_segment_2(f, out);
    REQUIRE(in.ooseq_segs() == 3);
    REQUIRE(out.ooseq_segs() == 1);
    REQUIRE(in.has_sacks());

    CHECK(buddy_ooseq_reclaim() == 3 * 1024);
    CHECK(in.pcb->ooseq == nullptr);
    CHECK_FALSE(in.has_sacks());
    CHECK(out.ooseq_segs() == 1); // outbound untouched while inbound had something
    CHECK(f.ack_now(in).sacks.empty());
    CHECK(MEMP_STATS_GET(used, MEMP_TCP_SEG) == 1);

    CHECK(buddy_ooseq_reclaim() == 1024);
    CHECK(out.pcb->ooseq == nullptr);
    CHECK_FALSE(out.has_sacks());
    CHECK(buddy_ooseq_reclaim() == 0);

    // The connection recovers normally afterwards (2..5 resent in order).
    for (int k = 2; k <= 5; k++) {
        f.inject(in, k);
    }
    CHECK(in.counters.recved_bytes == 5 * 1024);
}

namespace {
// Stands in for tcpip_try_callback(): records what pbuf_alloc_rx() would queue.
std::vector<void (*)(void *)> posted;
err_t post_result = ERR_OK;
int8_t fake_post(void (*callback)(void *)) {
    if (post_result == ERR_OK) {
        posted.push_back(callback);
    }
    return post_result;
}
} // namespace

TEST_CASE("ooseq_tcp T7: reclaim requests queue one callback until it runs", "[ooseq]") {
    Fixture f(FIX_A);
    Conn &in = f.open(HTTP_PORT);
    lose_segment_2(f, in);
    REQUIRE(in.ooseq_segs() == 3);

    posted.clear();
    post_result = ERR_OK;
    ooseq_limit::set_reclaim_poster_for_test(fake_post);

    // A burst of RX allocation failures queues the reclaim once.
    buddy_ooseq_reclaim_request();
    buddy_ooseq_reclaim_request();
    buddy_ooseq_reclaim_request();
    REQUIRE(posted.size() == 1);
    CHECK(in.ooseq_segs() == 3); // nothing freed until the tcpip thread runs it

    posted[0](nullptr);
    CHECK(in.pcb->ooseq == nullptr);
    CHECK_FALSE(in.has_sacks());

    // Once it has run, the next failure queues it again.
    buddy_ooseq_reclaim_request();
    CHECK(posted.size() == 2);
    posted[1](nullptr);

    // If the tcpip mailbox is full, nothing stays pending: the next failure retries.
    post_result = ERR_MEM;
    buddy_ooseq_reclaim_request();
    post_result = ERR_OK;
    buddy_ooseq_reclaim_request();
    CHECK(posted.size() == 3);
    posted[2](nullptr);

    ooseq_limit::set_reclaim_poster_for_test(nullptr);
}
