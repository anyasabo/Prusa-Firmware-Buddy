// EXPERIMENT: lib/WUI/upload_experiment.cpp with HAS_FORK_DIAGNOSTICS off, as on the MINI. The
// "upx-" switches must not change anything, and no experiment metric is sent. The upload fixes stay
// at their boot values.
#include <buddy/upload_experiment.hpp>
#include <buddy/rx_stats.h>
#include <option/has_fork_diagnostics.h>
#include "metric.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdarg>
#include <cstdio>

static_assert(!option::has_fork_diagnostics, "this test needs the stub option header in off/");

std::vector<RecordedMetric> recorded_metrics;

void metric_record_custom(metric_t *metric, const char *fmt, ...) {
    char buf[48];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    recorded_metrics.push_back({ metric->name, buf, false });
}

namespace {
uint32_t now_ms = 100'000;
int fix_a_calls = 0;
int prio_calls = 0;
} // namespace

extern "C" uint32_t ticks_ms() { return now_ms; }
extern "C" uint32_t ticks_us() { return now_ms * 1000; }
extern "C" uint32_t ticks_s() { return now_ms / 1000; }
void ooseq_limit::set_inbound_fix_a(bool) { fix_a_calls++; }
extern "C" void buddy_net_priority_raise(bool) { prio_calls++; }
extern "C" uint32_t heap_bytes_remaining() { return 12345; }
extern "C" uint32_t heap_malloc_free() { return 678; }
extern "C" void heap_malloc_chunks(uint32_t *free_chunks, uint32_t *largest_chunk) {
    *free_chunks = 1;
    *largest_chunk = 2;
}

TEST_CASE("without diagnostics, upx switches change nothing") {
    using namespace upload_experiment;
    recorded_metrics.clear();
    apply_from_path("/usb/upx-q8-a0-w0-f0-s0-e0-d0-n0-p0-v0-r0-y1-x16-m1460-x.gcode");
    CHECK(queue_cap == 33);
    CHECK(ooseq_fix_a);
    CHECK(async_prepare);
    CHECK(finish_async);
    CHECK(preview_after_upload);
    CHECK(tcp_wnd_kb == 8);
    CHECK(advertised_mss == 1024);
    CHECK_FALSE(net_prio_raised);
    CHECK(fix_a_calls == 0);
    CHECK(prio_calls == 0);
    CHECK(recorded_metrics.empty());
}

TEST_CASE("without diagnostics, no experiment metric is sent") {
    using namespace upload_experiment;
    recorded_metrics.clear();
    timing = {};
    timing.start_ms = 1;
    now_ms += 5'000;
    record_timing();
    record_http_step(50, 1, 2);
    buddy_tcpip_busy_record(50, 100, 0);
    buddy_net_lat_record(0, 5);
    buddy_net_lat_record(1, 5);
    now_ms += 10'000;
    report_usb_io();
    report_memory();
    report_net_lat();
    CHECK(recorded_metrics.empty());
}
