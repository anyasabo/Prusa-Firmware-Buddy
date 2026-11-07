// EXPERIMENT (upload-experiments branch): see buddy/upload_experiment.hpp.
#include <buddy/upload_experiment.hpp>

#include "metric.h"
#include <buddy/rx_stats.h>
#include <buddy/usb_io_stats.h>
#include <heap.h>
#include <timing.h>

#include <cstdlib>
#include <cstring>

namespace upload_experiment {

void apply_from_path(const char *path) {
    const char *p = path ? strstr(path, "upx-") : nullptr;
    if (!p) {
        return;
    }
    p += 4;
    while (*p && strchr("qawfsedumnpkvrhxoy", *p)) {
        const char key = *p++;
        char *end = nullptr;
        const long v = strtol(p, &end, 10);
        if (end == p) {
            break; // A letter without a number: not a switch, stop.
        }
        p = end;
        switch (key) {
        case 'q':
            queue_cap = v < 1 ? 1 : (v > 33 ? 33 : static_cast<size_t>(v));
            break;
        case 'a':
            ooseq_fix_a = v != 0;
            break;
        case 'w':
            async_prepare = v != 0;
            break;
        case 'f':
            name_check_in_memory = v != 0;
            break;
        case 's':
            lockfree_drive_check = v != 0;
            break;
        case 'e':
            answer_expect_continue = v != 0;
            break;
        case 'd':
            finish_async = v != 0;
            break;
        case 'u':
            if (v == 8 || v == 16 || v == 32) {
                usb_sectors_per_write = static_cast<size_t>(v);
            }
            break;
        case 'm':
            advertised_mss = static_cast<uint16_t>(v < 536 ? 536 : (v > 1460 ? 1460 : v));
            break;
        case 'n':
            sfn_on_worker = v != 0;
            break;
        case 'p':
            preview_no_wait = v != 0;
            break;
        case 'k':
            buddy_usbh_msc_wait_ms = static_cast<uint32_t>(v < 1 ? 1 : (v > 500 ? 500 : v));
            break;
        case 'v':
            preview_after_upload = v != 0;
            break;
        case 'r':
            rename_without_stat = v != 0;
            break;
        case 'h':
            buddy_fatfs_numname_hash_after = v != 0 ? 0 : 5;
            break;
        case 'x':
            if (v == 8 || v == 12 || v == 16) {
                tcp_wnd_kb = static_cast<uint32_t>(v);
            }
            break;
        case 'o':
            // At least 4 KB: below one full frame (~1.1 KB) nothing gets through,
            // not even the upload that would reset the switch.
            rx_ring_cap = v <= 0 ? 0 : static_cast<size_t>(v < 4 ? 4 : (v > 64 ? 64 : v)) * 1024;
            break;
        case 'y':
            net_prio_raised = v != 0;
            break;
        }
        if (*p == '-') {
            p++;
        }
    }
    ooseq_limit::set_inbound_fix_a(ooseq_fix_a);
    buddy_net_priority_raise(net_prio_raised); // on the tcpip thread (nhttp)
    METRIC_DEF(metric_upx, "upx", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
    // b: the boolean switches as bits, a=1 w=2 f=4 s=8 e=16 d=32 n=64 p=128
    // v=256 r=512 h=1024 (47 char limit).
    const unsigned bits = (ooseq_fix_a ? 1 : 0) | (async_prepare ? 2 : 0) | (name_check_in_memory ? 4 : 0)
        | (lockfree_drive_check ? 8 : 0) | (answer_expect_continue ? 16 : 0) | (finish_async ? 32 : 0)
        | (sfn_on_worker ? 64 : 0) | (preview_no_wait ? 128 : 0) | (preview_after_upload ? 256 : 0)
        | (rename_without_stat ? 512 : 0) | (buddy_fatfs_numname_hash_after == 0 ? 1024 : 0);
    metric_record_custom(&metric_upx, " q=%ui,b=%ui,u=%ui,m=%ui,k=%ui",
        static_cast<unsigned>(queue_cap), bits, static_cast<unsigned>(usb_sectors_per_write), static_cast<unsigned>(advertised_mss),
        static_cast<unsigned>(buddy_usbh_msc_wait_ms));
    METRIC_DEF(metric_upx2, "upx2", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
    metric_record_custom(&metric_upx2, " x=%ui,o=%ui,y=%ui", static_cast<unsigned>(tcp_wnd_kb),
        static_cast<unsigned>(rx_ring_cap / 1024), net_prio_raised ? 1u : 0u);
}

UsbSnapshot UsbSnapshot::now() {
    UsbSnapshot s;
    s.reads = buddy_usb_stats.reads;
    s.writes = buddy_usb_stats.writes;
    s.requests = s.reads + s.writes;
    s.busy_ms = buddy_usb_stats.busy_ms;
    s.wait_timeouts = buddy_usb_stats.wait_timeouts;
    s.lock_wait_ms = buddy_usb_stats.lock_wait_ms;
    return s;
}

void record_timing() {
    METRIC_DEF(metric_upx_t, "upx_t", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
    METRIC_DEF(metric_upx_f, "upx_f", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
    METRIC_DEF(metric_upx_u, "upx_u", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
    METRIC_DEF(metric_upx_n, "upx_n", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
    METRIC_DEF(metric_upx_v, "upx_v", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
    METRIC_DEF(metric_upx_g, "upx_g", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
    // Caps keep every value within the 47 character limit of custom metrics:
    // 5 fields of " x=99999i" are 45 characters.
    const auto cap = [](uint32_t v) { return static_cast<unsigned>(v > 99999 ? 99999 : v); };
    metric_record_custom(&metric_upx_t, " c=%ui,k=%ui,r=%ui,f=%ui,q=%ui",
        cap(timing.chk_ms), cap(timing.cre_ms), cap(timing.rdy_ms), cap(timing.fin_ms), cap(timing.queue_ms));
    metric_record_custom(&metric_upx_f, " m=%ui,y=%ui,c=%ui,r=%ui,s=%ui",
        cap(timing.mkdir_ms), cap(timing.sync_ms), cap(timing.close_ms), cap(timing.rename_ms), cap(timing.sfn_worker_ms));
    const uint32_t timeouts = UsbSnapshot::now().wait_timeouts - timing.usb_at_start.wait_timeouts;
    metric_record_custom(&metric_upx_u, " p=%ui,o=%ui,f=%ui,x=%ui,w=%ui",
        cap(timing.prep_usb_ms), cap(timing.prep_usb_req), cap(timing.fin_usb_ms), cap(timing.fin_usb_req), cap(timeouts));
    metric_record_custom(&metric_upx_n, " s=%ui,n=%ui,l=%ui",
        cap(timing.sfn_tcpip_ms), cap(timing.notify_ms), static_cast<unsigned>(timing.notify_mode));
    metric_record_custom(&metric_upx_v, " c=%ui,a=%ui,b=%ui,p=%ui,f=%ui",
        cap(timing.check_usb_req), cap(timing.fin_reads), cap(timing.fin_writes), cap(timing.prep_lock_ms), cap(timing.fin_lock_ms));
    metric_record_custom(&metric_upx_g, " m=%ui,y=%ui,c=%ui,r=%ui,s=%ui",
        cap(timing.mkdir_req), cap(timing.sync_req), cap(timing.close_req), cap(timing.rename_req), cap(timing.sfn_req));
}

void report_memory() {
    static uint32_t last_ms = 0;
    const uint32_t now = ticks_ms();
    if (now - last_ms < 5000) {
        return;
    }
    last_ms = now;
    METRIC_DEF(metric_heap_x, "heap_x", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
    METRIC_DEF(metric_rx_ring, "rx_ring", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
    uint32_t free_chunks = 0;
    uint32_t largest_chunk = 0;
    heap_malloc_chunks(&free_chunks, &largest_chunk);
    metric_record_custom(&metric_heap_x, " s=%ui,f=%ui,o=%ui,l=%ui", static_cast<unsigned>(heap_bytes_remaining()), static_cast<unsigned>(heap_malloc_free()),
        static_cast<unsigned>(free_chunks), static_cast<unsigned>(largest_chunk));
    const uint32_t peak = buddy_rx_stats.ring_peak;
    buddy_rx_stats.ring_peak = 0; // racy with the allocator; a missed peak only shows next time
    metric_record_custom(&metric_rx_ring, " p=%ui", static_cast<unsigned>(peak));
}

namespace {
    // net_lat: kind 0 written by the network task, kind 1 by the tcpip thread;
    // reported by the tcpip thread.
    struct LatStats {
        uint32_t count = 0;
        uint32_t sum = 0;
        uint32_t max = 0;
    };
    LatStats lat[2];
    LatStats lat_last[2];
} // namespace

void report_net_lat() {
    static uint32_t last_ms = 0;
    const uint32_t now = ticks_ms();
    if (now - last_ms < 1000) {
        return;
    }
    last_ms = now;
    const LatStats w = lat[0], q = lat[1];
    const uint32_t wn = w.count - lat_last[0].count, qn = q.count - lat_last[1].count;
    const uint32_t ws = w.sum - lat_last[0].sum, qs = q.sum - lat_last[1].sum;
    // max is reset by the reader (racy, a missed max only shows next time)
    lat[0].max = 0;
    lat[1].max = 0;
    lat_last[0] = w;
    lat_last[1] = q;
    if (wn == 0 && qn == 0) {
        return;
    }
    METRIC_DEF(metric_net_lat, "net_lat", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
    // e wake-ups, w their total ms, a the longest; n packets, q their total
    // mailbox ms, b the longest. 6 fields: counts/totals capped at 9999, maxima
    // at 999 (worst case 46 characters, limit 47).
    const auto cap4 = [](uint32_t v) { return static_cast<unsigned>(v > 9999 ? 9999 : v); };
    const auto cap3 = [](uint32_t v) { return static_cast<unsigned>(v > 999 ? 999 : v); };
    metric_record_custom(&metric_net_lat, " e=%ui,w=%ui,a=%ui,n=%ui,q=%ui,b=%ui",
        cap4(wn), cap4(ws), cap3(w.max), cap4(qn), cap4(qs), cap3(q.max));
}

void report_usb_io() {
    static uint32_t last_ms = 0;
    static bool started = false;
    static struct buddy_usb_stats last {};
    const uint32_t now = ticks_ms();
    if (started && now - last_ms < 1000) {
        return;
    }
    last_ms = now;
    if (!started) {
        // The first call only takes the baseline (not everything since boot).
        started = true;
        last = buddy_usb_stats;
        buddy_usb_stats.max_ms = 0;
        return;
    }
    // Plain reads of counters another task increments: each value is a single
    // aligned word, a report may be off by the request in flight.
    const struct buddy_usb_stats cur = buddy_usb_stats;
    const uint32_t max_ms = cur.max_ms;
    buddy_usb_stats.max_ms = 0; // may lose a concurrent update; good enough here
    const uint32_t reads = cur.reads - last.reads;
    const uint32_t writes = cur.writes - last.writes;
    const uint32_t busy = cur.busy_ms - last.busy_ms;
    const uint32_t timeouts = cur.wait_timeouts - last.wait_timeouts;
    const uint32_t lock_waits = cur.lock_waits - last.lock_waits;
    const struct buddy_usb_stats prev = last;
    last = cur;
    if (reads == 0 && writes == 0 && timeouts == 0 && lock_waits == 0) {
        return;
    }
    METRIC_DEF(metric_usb_io, "usb_io", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
    METRIC_DEF(metric_usb_io2, "usb_io2", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
    METRIC_DEF(metric_usb_io3, "usb_io3", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
    const auto cap = [](uint32_t v) { return static_cast<unsigned>(v > 99999 ? 99999 : v); };
    metric_record_custom(&metric_usb_io, " r=%ui,w=%ui,t=%ui,m=%ui,o=%ui",
        cap(reads), cap(writes), cap(busy), cap(max_ms), cap(timeouts));
    // s sectors, g single-sector requests, l requests >= 20 ms, e URB events,
    // n of those URB_NOTREADY.
    metric_record_custom(&metric_usb_io2, " s=%ui,g=%ui,l=%ui,e=%ui,n=%ui",
        cap(cur.sectors - prev.sectors), cap(cur.single_sector - prev.single_sector), cap(cur.slow - prev.slow),
        cap(cur.urb_events - prev.urb_events), cap(cur.urb_notready - prev.urb_notready));
    // u unaligned (split) requests, x errors, c FatFs lock waits, d ms waited.
    metric_record_custom(&metric_usb_io3, " u=%ui,x=%ui,c=%ui,d=%ui",
        cap(cur.unaligned - prev.unaligned), cap(cur.errors - prev.errors), cap(lock_waits),
        cap(cur.lock_wait_ms - prev.lock_wait_ms));
}

namespace {

    // Longest busy stretch per (about) one second, plus how many were long.
    struct Window {
        uint32_t start_ms = 0;
        uint32_t max_ms = 0;
        uint32_t over_10ms = 0;
        uint32_t over_100ms = 0;
        uint32_t tag_a = 0;
        uint32_t tag_b = 0;

        // Returns true (and leaves the finished window in *done) when one
        // second passed since the window started; the caller reports it.
        bool add(uint32_t ms, uint32_t a, uint32_t b, Window *done) {
            const uint32_t now = ticks_ms();
            bool finished = false;
            if (now - start_ms >= 1000) {
                if (max_ms > 0) {
                    *done = *this;
                    finished = true;
                }
                *this = Window {};
                start_ms = now;
            }
            if (ms >= 10) {
                over_10ms++;
            }
            if (ms >= 100) {
                over_100ms++;
            }
            if (ms > max_ms) {
                max_ms = ms;
                tag_a = a;
                tag_b = b;
            }
            return finished;
        }
    };

    Window tcpip_window;
    Window http_window;

} // namespace

void record_http_step(uint32_t ms, uint8_t handler_before, uint8_t handler_after) {
    Window done;
    if (http_window.add(ms, handler_before, handler_after, &done)) {
        METRIC_DEF(metric_http_blk, "http_blk", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
        metric_record_custom(&metric_http_blk, " m=%ui,h=%ui,g=%ui,n=%ui,x=%ui",
            static_cast<unsigned>(done.max_ms), static_cast<unsigned>(done.tag_a), static_cast<unsigned>(done.tag_b),
            static_cast<unsigned>(done.over_10ms), static_cast<unsigned>(done.over_100ms));
    }
}

} // namespace upload_experiment

extern "C" void buddy_tcpip_busy_record(uint32_t ms, uint8_t kind, uint32_t fn) {
    using upload_experiment::Window;
    upload_experiment::report_usb_io();
    upload_experiment::report_net_lat();
    upload_experiment::report_memory();
    Window done;
    if (upload_experiment::tcpip_window.add(ms, kind, fn, &done)) {
        METRIC_DEF(metric_tcpip_blk, "tcpip_blk", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
        // f is the callback address (hex in the ELF's symbol table).
        metric_record_custom(&metric_tcpip_blk, " m=%ui,k=%ui,a=%ui,b=%ui,f=%ui",
            static_cast<unsigned>(done.max_ms), static_cast<unsigned>(done.tag_a),
            static_cast<unsigned>(done.over_10ms), static_cast<unsigned>(done.over_100ms), static_cast<unsigned>(done.tag_b));
    }
}

extern "C" void buddy_net_lat_record(uint8_t kind, uint32_t ms) {
    if (kind > 1) {
        return;
    }
    auto &l = upload_experiment::lat[kind];
    l.count++;
    l.sum += ms;
    if (ms > l.max) {
        l.max = ms;
    }
}

extern "C" uint32_t buddy_tcp_wnd(void) {
    return upload_experiment::tcp_wnd_kb * 1024;
}

extern "C" uint16_t buddy_advertised_mss(void) {
    return upload_experiment::advertised_mss;
}
