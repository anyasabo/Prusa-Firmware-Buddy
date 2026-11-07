// EXPERIMENT (upload-experiments branch): see buddy/upload_experiment.hpp.
#include <buddy/upload_experiment.hpp>

#include "metric.h"
#include <buddy/rx_stats.h>
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
    while (*p && strchr("qawfsedum", *p)) {
        const char key = *p++;
        char *end = nullptr;
        const long v = strtol(p, &end, 10);
        if (end == p) {
            break; // A letter without a number: not a switch, stop.
        }
        p = end;
        switch (key) {
        case 'q':
            queue_cap = v < 1 ? 1 : (v > 17 ? 17 : static_cast<size_t>(v));
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
        }
        if (*p == '-') {
            p++;
        }
    }
    ooseq_limit::set_inbound_fix_a(ooseq_fix_a);
    METRIC_DEF(metric_upx, "upx", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
    // b: the boolean switches as bits, a=1 w=2 f=4 s=8 e=16 d=32 (47 char limit).
    const unsigned bits = (ooseq_fix_a ? 1 : 0) | (async_prepare ? 2 : 0) | (name_check_in_memory ? 4 : 0)
        | (lockfree_drive_check ? 8 : 0) | (answer_expect_continue ? 16 : 0) | (finish_async ? 32 : 0);
    metric_record_custom(&metric_upx, " q=%ui,b=%ui,u=%ui,m=%ui",
        static_cast<unsigned>(queue_cap), bits, static_cast<unsigned>(usb_sectors_per_write), static_cast<unsigned>(advertised_mss));
}

void record_timing() {
    METRIC_DEF(metric_upx_t, "upx_t", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
    const auto cap = [](uint32_t v) { return static_cast<unsigned>(v > 99999 ? 99999 : v); };
    metric_record_custom(&metric_upx_t, " c=%ui,k=%ui,r=%ui,f=%ui,q=%ui",
        cap(timing.chk_ms), cap(timing.cre_ms), cap(timing.rdy_ms), cap(timing.fin_ms), cap(timing.queue_ms));
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
    Window done;
    if (upload_experiment::tcpip_window.add(ms, kind, fn, &done)) {
        METRIC_DEF(metric_tcpip_blk, "tcpip_blk", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
        // f is the callback address (hex in the ELF's symbol table).
        metric_record_custom(&metric_tcpip_blk, " m=%ui,k=%ui,a=%ui,b=%ui,f=%ui",
            static_cast<unsigned>(done.max_ms), static_cast<unsigned>(done.tag_a),
            static_cast<unsigned>(done.over_10ms), static_cast<unsigned>(done.over_100ms), static_cast<unsigned>(done.tag_b));
    }
}

extern "C" uint16_t buddy_advertised_mss(void) {
    return upload_experiment::advertised_mss;
}
