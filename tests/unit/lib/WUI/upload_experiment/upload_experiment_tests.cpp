// EXPERIMENT (upload-experiments): switch parsing and metric formatting of
// lib/WUI/upload_experiment.cpp.
#include <buddy/upload_experiment.hpp>
#include <buddy/rx_stats.h>
#include <buddy/usb_io_stats.h>
#include "metric.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <cstdarg>
#include <cstdio>
#include <optional>

// Declared in include/buddy/lwipopts.h, which pulls in lwIP; declare it here instead.
extern "C" uint32_t buddy_tcp_wnd(void);

std::vector<RecordedMetric> recorded_metrics;

void metric_record_custom(metric_t *metric, const char *fmt, ...) {
    char buf[48];
    va_list args;
    va_start(args, fmt);
    const int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    recorded_metrics.push_back({ metric->name, buf, static_cast<size_t>(len) >= sizeof(buf) });
}

namespace {
uint32_t now_ms = 100'000;
bool fix_a = false;
bool prio_raised = false;

std::optional<RecordedMetric> last(const char *name) {
    for (auto it = recorded_metrics.rbegin(); it != recorded_metrics.rend(); ++it) {
        if (it->name == name) {
            return *it;
        }
    }
    return std::nullopt;
}

void reset_switches() {
    using namespace upload_experiment;
    queue_cap = 33;
    tcp_wnd_kb = 8;
    rx_ring_cap = 0;
    net_prio_raised = false;
    prio_raised = false;
    ooseq_fix_a = true;
    async_prepare = true;
    name_check_in_memory = true;
    lockfree_drive_check = true;
    answer_expect_continue = true;
    finish_async = true;
    advertised_mss = 1024;
    sfn_on_worker = true;
    preview_no_wait = true;
    preview_after_upload = true;
    rename_without_stat = true;
    usb_sectors_per_write = 8;
    buddy_usbh_msc_wait_ms = 500;
    buddy_fatfs_numname_hash_after = 5;
    recorded_metrics.clear();
}
} // namespace

extern "C" uint32_t ticks_ms() { return now_ms; }
extern "C" uint32_t ticks_us() { return now_ms * 1000; }
extern "C" uint32_t ticks_s() { return now_ms / 1000; }
void ooseq_limit::set_inbound_fix_a(bool on) { fix_a = on; }
extern "C" void buddy_net_priority_raise(bool raised) { prio_raised = raised; }
extern "C" uint32_t heap_bytes_remaining() { return 12345; }
extern "C" uint32_t heap_malloc_free() { return 678; }
extern "C" void heap_malloc_chunks(uint32_t *free_chunks, uint32_t *largest_chunk) {
    // Realistic worst case for the metric length (192 KB RAM): 6-digit sizes, 5-digit count.
    *free_chunks = 99999;
    *largest_chunk = 196608;
}

TEST_CASE("upx switches parse from the file name") {
    using namespace upload_experiment;
    reset_switches();

    SECTION("stock") {
        apply_from_path("/usb/upx-q8-a0-w0-f0-s0-e0-d0-n0-p0-k500-v1-r0-h0-u8-m1024-x.gcode");
        CHECK(queue_cap == 8);
        CHECK_FALSE(ooseq_fix_a);
        CHECK_FALSE(fix_a);
        CHECK_FALSE(async_prepare);
        CHECK_FALSE(name_check_in_memory);
        CHECK_FALSE(lockfree_drive_check);
        CHECK_FALSE(answer_expect_continue);
        CHECK_FALSE(finish_async);
        CHECK_FALSE(sfn_on_worker);
        CHECK_FALSE(preview_no_wait);
        CHECK(buddy_usbh_msc_wait_ms == 500);
        CHECK(preview_after_upload);
        CHECK_FALSE(rename_without_stat);
        CHECK(buddy_fatfs_numname_hash_after == 5);
        CHECK(usb_sectors_per_write == 8);
        CHECK(advertised_mss == 1024);
        auto upx = last("upx");
        REQUIRE(upx);
        CHECK(upx->value == " q=8i,b=256i,u=8i,m=1024i,k=500i");
    }

    SECTION("everything on, short USB wait") {
        apply_from_path("upx-n1-p1-k2-h1.gcode");
        CHECK(sfn_on_worker);
        CHECK(preview_no_wait);
        CHECK(buddy_usbh_msc_wait_ms == 2);
        CHECK(buddy_fatfs_numname_hash_after == 0);
        auto upx = last("upx");
        REQUIRE(upx);
        CHECK(upx->value == " q=33i,b=2047i,u=8i,m=1024i,k=2i");
    }

    SECTION("v0 and h back to stock") {
        apply_from_path("upx-v0-h1-x");
        CHECK_FALSE(preview_after_upload);
        CHECK(buddy_fatfs_numname_hash_after == 0);
        apply_from_path("upx-h0-x");
        CHECK(buddy_fatfs_numname_hash_after == 5);
    }

    SECTION("window, ring cap, priority") {
        apply_from_path("upx-x16-o11-y1-q40-x");
        CHECK(tcp_wnd_kb == 16);
        CHECK(buddy_tcp_wnd() == 16384);
        CHECK(rx_ring_cap == 11 * 1024);
        CHECK(net_prio_raised);
        CHECK(prio_raised);
        CHECK(queue_cap == 33); // clamped
        auto upx2 = last("upx2");
        REQUIRE(upx2);
        CHECK(upx2->value == " x=16i,o=11i,y=1i");
        apply_from_path("upx-o1-x");
        CHECK(rx_ring_cap == 4 * 1024); // minimum
        apply_from_path("upx-x13-o0-y0-x"); // x13 isn't a valid window: kept
        CHECK(tcp_wnd_kb == 16);
        CHECK(rx_ring_cap == 0);
        CHECK_FALSE(prio_raised);
        apply_from_path("upx-x8-x");
        CHECK(buddy_tcp_wnd() == 8192);
    }

    SECTION("a prefix before upx- is fine") {
        apply_from_path("/usb/a3f9c1-upx-n0-x.gcode");
        CHECK_FALSE(sfn_on_worker);
    }

    SECTION("k is clamped to 1..500") {
        apply_from_path("upx-k0-x");
        CHECK(buddy_usbh_msc_wait_ms == 1);
        apply_from_path("upx-k100000-x");
        CHECK(buddy_usbh_msc_wait_ms == 500);
    }

    SECTION("a letter without a number stops the parsing") {
        apply_from_path("upx-n0-pk1-x");
        CHECK_FALSE(sfn_on_worker);
        CHECK(preview_no_wait);
        CHECK(buddy_usbh_msc_wait_ms == 500);
    }

    SECTION("not an experiment upload") {
        apply_from_path("/usb/benchy-n0.gcode");
        CHECK(sfn_on_worker);
        CHECK(recorded_metrics.empty());
    }

    SECTION("upx fits at the largest values") {
        apply_from_path("upx-q17-u32-m1460-k500-h1-x");
        auto upx = last("upx");
        REQUIRE(upx);
        CHECK_FALSE(upx->too_long);
    }
}

TEST_CASE("per-upload metrics fit the 47 character limit") {
    using namespace upload_experiment;
    reset_switches();
    buddy_usb_stats = {};
    timing = {};
    const uint32_t huge = 4'000'000'000u;
    timing.chk_ms = timing.cre_ms = timing.rdy_ms = timing.fin_ms = timing.queue_ms = huge;
    timing.mkdir_ms = timing.sync_ms = timing.close_ms = timing.rename_ms = timing.sfn_worker_ms = huge;
    timing.prep_usb_ms = timing.prep_usb_req = timing.fin_usb_ms = timing.fin_usb_req = huge;
    timing.sfn_tcpip_ms = timing.notify_ms = huge;
    timing.notify_mode = notify_autostart;
    timing.check_usb_req = timing.fin_reads = timing.fin_writes = timing.prep_lock_ms = timing.fin_lock_ms = huge;
    timing.mkdir_req = timing.sync_req = timing.close_req = timing.rename_req = timing.sfn_req = huge;
    buddy_usb_stats.wait_timeouts = huge;
    record_timing();
    for (const char *name : { "upx_t", "upx_f", "upx_u", "upx_n", "upx_v", "upx_g" }) {
        auto m = last(name);
        REQUIRE(m);
        INFO(name << ": " << m->value);
        CHECK_FALSE(m->too_long);
    }
    CHECK(last("upx_f")->value == " m=99999i,y=99999i,c=99999i,r=99999i,s=99999i");
    CHECK(last("upx_u")->value == " p=99999i,o=99999i,f=99999i,x=99999i,w=99999i");
    CHECK(last("upx_n")->value == " s=99999i,n=99999i,l=5i");
    CHECK(last("upx_v")->value == " c=99999i,a=99999i,b=99999i,p=99999i,f=99999i");
    CHECK(last("upx_g")->value == " m=99999i,y=99999i,c=99999i,r=99999i,s=99999i");
}

TEST_CASE("upx_u counts MSC wait timeouts since the upload started") {
    using namespace upload_experiment;
    reset_switches();
    buddy_usb_stats = {};
    buddy_usb_stats.wait_timeouts = 7;
    timing = {};
    timing.usb_at_start = UsbSnapshot::now();
    buddy_usb_stats.wait_timeouts = 10;
    buddy_usb_stats.reads = 3;
    buddy_usb_stats.writes = 2;
    buddy_usb_stats.busy_ms = 40;
    const auto snap = UsbSnapshot::now();
    CHECK(snap.requests == 5);
    CHECK(snap.busy_ms == 40);
    record_timing();
    CHECK(last("upx_u")->value == " p=0i,o=0i,f=0i,x=0i,w=3i");
}

TEST_CASE("usb_io reports deltas about once a second") {
    using namespace upload_experiment;
    reset_switches();
    buddy_usb_stats = {};
    // Align with the reporter's own clock: one report call to start a window.
    now_ms += 10'000;
    report_usb_io();
    recorded_metrics.clear();

    SECTION("nothing to report without I/O") {
        now_ms += 1500;
        report_usb_io();
        CHECK(recorded_metrics.empty());
    }

    SECTION("deltas, max reset, gated to 1 s") {
        buddy_usb_stats.reads += 10;
        buddy_usb_stats.writes += 4;
        buddy_usb_stats.busy_ms += 300;
        buddy_usb_stats.max_ms = 120;
        buddy_usb_stats.wait_timeouts += 1;
        now_ms += 500;
        report_usb_io();
        CHECK(recorded_metrics.empty()); // not a second yet
        now_ms += 600;
        report_usb_io();
        auto m = last("usb_io");
        REQUIRE(m);
        CHECK(m->value == " r=10i,w=4i,t=300i,m=120i,o=1i");
        auto m2 = last("usb_io2");
        REQUIRE(m2);
        CHECK(m2->value == " s=0i,g=0i,l=0i,e=0i,n=0i");
        CHECK(buddy_usb_stats.max_ms == 0);

        recorded_metrics.clear();
        buddy_usb_stats.writes += 1;
        buddy_usb_stats.busy_ms += 5;
        buddy_usb_stats.max_ms = 5;
        now_ms += 1000;
        report_usb_io();
        m = last("usb_io");
        REQUIRE(m);
        CHECK(m->value == " r=0i,w=1i,t=5i,m=5i,o=0i");
    }

    SECTION("lock waits alone are reported") {
        buddy_usb_stats.lock_waits += 2;
        buddy_usb_stats.lock_wait_ms += 340;
        now_ms += 1000;
        report_usb_io();
        auto m3 = last("usb_io3");
        REQUIRE(m3);
        CHECK(m3->value == " u=0i,x=0i,c=2i,d=340i");
    }

    SECTION("fits at the largest values") {
        buddy_usb_stats.sectors += 4'000'000'000u;
        buddy_usb_stats.single_sector += 4'000'000'000u;
        buddy_usb_stats.slow += 4'000'000'000u;
        buddy_usb_stats.urb_events += 4'000'000'000u;
        buddy_usb_stats.urb_notready += 4'000'000'000u;
        buddy_usb_stats.unaligned += 4'000'000'000u;
        buddy_usb_stats.errors += 4'000'000'000u;
        buddy_usb_stats.lock_waits += 4'000'000'000u;
        buddy_usb_stats.lock_wait_ms += 4'000'000'000u;
        buddy_usb_stats.reads += 4'000'000'000u;
        buddy_usb_stats.writes += 4'000'000'000u;
        buddy_usb_stats.busy_ms += 4'000'000'000u;
        buddy_usb_stats.max_ms = 4'000'000'000u;
        buddy_usb_stats.wait_timeouts += 4'000'000'000u;
        now_ms += 1000;
        report_usb_io();
        for (const char *name : { "usb_io", "usb_io2", "usb_io3" }) {
            auto m = last(name);
            REQUIRE(m);
            INFO(name << ": " << m->value);
            CHECK_FALSE(m->too_long);
        }
    }
}

TEST_CASE("net_lat and memory metrics") {
    using namespace upload_experiment;
    reset_switches();
    now_ms += 10'000;
    report_net_lat();
    report_memory();
    recorded_metrics.clear();

    SECTION("net_lat sums, maxima, counts per second") {
        buddy_net_lat_record(0, 3);
        buddy_net_lat_record(0, 30);
        buddy_net_lat_record(1, 1);
        buddy_net_lat_record(1, 2);
        buddy_net_lat_record(1, 40);
        buddy_net_lat_record(7, 999); // unknown kind: ignored
        now_ms += 1000;
        report_net_lat();
        auto m = last("net_lat");
        REQUIRE(m);
        CHECK(m->value == " e=2i,w=33i,a=30i,n=3i,q=43i,b=40i");
        recorded_metrics.clear();
        now_ms += 1000;
        report_net_lat();
        CHECK_FALSE(last("net_lat")); // no traffic, nothing reported
    }

    SECTION("net_lat fits at the largest values") {
        for (int i = 0; i < 12000; i++) { // counts and totals past their caps
            buddy_net_lat_record(0, 300'000);
            buddy_net_lat_record(1, 300'000);
        }
        now_ms += 1000;
        report_net_lat();
        auto m = last("net_lat");
        REQUIRE(m);
        INFO(m->value);
        CHECK_FALSE(m->too_long);
        CHECK(m->value == " e=9999i,w=9999i,a=999i,n=9999i,q=9999i,b=999i");
    }

    SECTION("heap_x and rx_ring every 5 s") {
        buddy_rx_stats.ring_peak = 4'000'000'000u;
        now_ms += 4000;
        report_memory();
        CHECK_FALSE(last("heap_x"));
        now_ms += 1000;
        report_memory();
        auto h = last("heap_x");
        REQUIRE(h);
        CHECK(h->value == " s=12345i,f=678i,o=99999i,l=196608i");
        CHECK_FALSE(h->too_long);
        auto r = last("rx_ring");
        REQUIRE(r);
        CHECK(r->value == " p=4000000000i");
        CHECK(buddy_rx_stats.ring_peak == 0);
    }
}
