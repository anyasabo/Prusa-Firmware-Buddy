#include <ringdown.hpp>

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <limits>
#include <vector>

using namespace indx_head::heater;
using indx_head::NozzlePresence;

namespace {

using Buffer = std::array<uint16_t, sample_buffer_len>;

struct Ringdown {
    Buffer samples;
    std::vector<uint16_t> peaks; // the peak sample of each cycle
};

/// A triangle ringdown around dc: each cycle has a negative half and then a positive half,
/// with the amplitude amplitudes[n] in cycle n. The rising zero crossings are exactly on a
/// sample, so the interval is period_samples * adc_sample_cycles.
Ringdown triangle_ringdown(uint16_t dc, std::vector<int> amplitudes, int period_samples = 16, int start = 5) {
    Ringdown r;
    r.samples.fill(dc);
    const int half = period_samples / 2;
    int i = start;
    for (const int a : amplitudes) {
        for (int j = 0; j < period_samples && i < static_cast<int>(sample_buffer_len); ++j, ++i) {
            const int k = j % half;
            const int tri = (k <= half / 2) ? k : half - k; // 0 .. half/2 .. 1
            const int v = a * tri / (half / 2);
            r.samples[i] = static_cast<uint16_t>(j < half ? dc - v : dc + v);
        }
        r.peaks.push_back(static_cast<uint16_t>(dc + a));
    }
    return r;
}

std::vector<int> geometric(int a0, double q, int n) {
    std::vector<int> result;
    double a = a0;
    for (int i = 0; i < n; ++i) {
        result.push_back(static_cast<int>(a));
        a *= q;
    }
    return result;
}

/// The decay formula of the firmware, over the peaks from first.
float expected_decay(const std::vector<uint16_t> &peaks, size_t first) {
    float sum = 0;
    for (size_t i = first; i < first + avg_peaks; ++i) {
        sum += static_cast<float>(peaks[i + 1]) / peaks[i];
    }
    return 1.0f - sum / avg_peaks;
}

} // namespace

TEST_CASE("indx_head ringdown: presence thresholds", "[indx_head][ringdown]") {
    CHECK(classify_decay(0.096f) == NozzlePresence::present);
    CHECK(classify_decay(nozzle_present_decay_threshold) == NozzlePresence::present);
    CHECK(classify_decay(std::nextafter(nozzle_present_decay_threshold, 0.0f)) == NozzlePresence::unknown);
    CHECK(classify_decay(0.06f) == NozzlePresence::unknown);
    CHECK(classify_decay(nozzle_absent_decay_threshold) == NozzlePresence::unknown);
    CHECK(classify_decay(std::nextafter(nozzle_absent_decay_threshold, 0.0f)) == NozzlePresence::absent);
    CHECK(classify_decay(0.029f) == NozzlePresence::absent);
    CHECK(classify_decay(-0.5f) == NozzlePresence::absent);
    CHECK(classify_decay(1.0f) == NozzlePresence::present);
    // Never heat on a NaN
    CHECK(classify_decay(std::numeric_limits<float>::quiet_NaN()) == NozzlePresence::unknown);
    CHECK(classify_decay(std::numeric_limits<float>::infinity()) == NozzlePresence::present);
    CHECK(classify_decay(-std::numeric_limits<float>::infinity()) == NozzlePresence::absent);
}

TEST_CASE("indx_head ringdown: DC offset is the mean", "[indx_head][ringdown]") {
    Buffer b;
    b.fill(2000);
    CHECK(dc_offset(b) == 2000);
    b[0] = 2000 + 255;
    CHECK(dc_offset(b) == 2000);
    b[1] = 2001;
    CHECK(dc_offset(b) == 2001);
    b.fill(4095);
    CHECK(dc_offset(b) == 4095);
}

TEST_CASE("indx_head ringdown: a flat signal has no peaks", "[indx_head][ringdown]") {
    Buffer b;
    b.fill(2000);
    const auto a = analyze_ringdown(b, 2000);
    CHECK(a.status == RingdownAnalysisStatus::NOT_ENOUGH_PEAKS);
    CHECK(a.nozzle_presence == NozzlePresence::unknown);
    CHECK(a.interval == 0);
    CHECK(a.decay == 0);
}

TEST_CASE("indx_head ringdown: too few cycles", "[indx_head][ringdown]") {
    const int cycles = GENERATE(1, 5, 9);
    const auto r = triangle_ringdown(2000, geometric(1000, 0.9, cycles));
    CHECK(analyze_ringdown(r.samples, 2000).status == RingdownAnalysisStatus::NOT_ENOUGH_PEAKS);
}

TEST_CASE("indx_head ringdown: a small swing does not start the analysis", "[indx_head][ringdown]") {
    // The first swing must go at least 200 below the DC offset
    const auto small = triangle_ringdown(2000, std::vector<int>(14, 199));
    CHECK(analyze_ringdown(small.samples, 2000).status == RingdownAnalysisStatus::NOT_ENOUGH_PEAKS);
    const auto at_limit = triangle_ringdown(2000, std::vector<int>(14, 200));
    CHECK(analyze_ringdown(at_limit.samples, 2000).status == RingdownAnalysisStatus::VALID);
}

TEST_CASE("indx_head ringdown: a low DC offset never starts the analysis", "[indx_head][ringdown]") {
    const auto r = triangle_ringdown(150, geometric(140, 0.9, 14));
    CHECK(analyze_ringdown(r.samples, 150).status == RingdownAnalysisStatus::NOT_ENOUGH_PEAKS);
}

TEST_CASE("indx_head ringdown: interval and decay of a seated nozzle", "[indx_head][ringdown]") {
    const int period = GENERATE(12, 16, 20);
    const auto r = triangle_ringdown(2000, geometric(1200, 0.6, 14), period);
    const auto a = analyze_ringdown(r.samples, 2000);
    REQUIRE(a.status == RingdownAnalysisStatus::VALID);
    CHECK(a.interval == period * adc_sample_cycles);
    CHECK(a.decay == Catch::Approx(expected_decay(r.peaks, 0)));
    CHECK(a.decay > nozzle_present_decay_threshold);
    CHECK(a.nozzle_presence == NozzlePresence::present);
}

TEST_CASE("indx_head ringdown: no nozzle", "[indx_head][ringdown]") {
    const auto r = triangle_ringdown(2000, geometric(1200, 0.95, 14));
    const auto a = analyze_ringdown(r.samples, 2000);
    REQUIRE(a.status == RingdownAnalysisStatus::VALID);
    CHECK(a.decay < nozzle_absent_decay_threshold);
    CHECK(a.nozzle_presence == NozzlePresence::absent);
}

TEST_CASE("indx_head ringdown: a distorted first peak is skipped", "[indx_head][ringdown]") {
    auto amplitudes = geometric(1200, 0.6, 14);
    amplitudes[0] = 600; // smaller than the second peak
    const auto r = triangle_ringdown(2000, amplitudes);
    const auto a = analyze_ringdown(r.samples, 2000);
    REQUIRE(a.status == RingdownAnalysisStatus::VALID);
    CHECK(a.decay == Catch::Approx(expected_decay(r.peaks, 1)));
}

TEST_CASE("indx_head ringdown: peaks at twice the DC offset are skipped", "[indx_head][ringdown]") {
    // The first 2 peaks are still driven: at 2 * dc
    const auto r = triangle_ringdown(400, { 400, 400, 300, 225, 170, 128, 96, 72, 54, 40, 30 });
    const auto a = analyze_ringdown(r.samples, 400);
    REQUIRE(a.status == RingdownAnalysisStatus::VALID);
    CHECK(a.decay == Catch::Approx(expected_decay(r.peaks, 2)));
}

TEST_CASE("indx_head ringdown: at most 6 peaks are skipped", "[indx_head][ringdown]") {
    // Each peak is smaller than the next: the skip stops at 6 and the decay is negative
    std::vector<int> amplitudes;
    for (int i = 0; i < 14; ++i) {
        amplitudes.push_back(400 + 40 * i);
    }
    const auto r = triangle_ringdown(2000, amplitudes);
    const auto a = analyze_ringdown(r.samples, 2000);
    REQUIRE(a.status == RingdownAnalysisStatus::VALID);
    CHECK(a.decay == Catch::Approx(expected_decay(r.peaks, 6)));
    CHECK(a.nozzle_presence == NozzlePresence::absent);
}

TEST_CASE("indx_head ringdown: the interval limits", "[indx_head][ringdown]") {
    auto check = [](uint16_t interval, uint16_t last, RingdownAnalysisStatus expected) {
        RingdownAnalysis a { .status = RingdownAnalysisStatus::VALID, .interval = interval, .decay = 0.1f, .nozzle_presence = NozzlePresence::present, .time = 0 };
        const bool ok = ringdown_analysis_sanity_check(a, last);
        CHECK(ok == (expected == RingdownAnalysisStatus::VALID));
        CHECK(a.status == expected);
        return last;
    };
    CHECK(check(169, 0, RingdownAnalysisStatus::INTERVAL_TOO_SHORT) == 0);
    CHECK(check(0, 0, RingdownAnalysisStatus::INTERVAL_TOO_SHORT) == 0);
    CHECK(check(170, 0, RingdownAnalysisStatus::VALID) == 170);
    CHECK(check(300, 0, RingdownAnalysisStatus::VALID) == 300);
    CHECK(check(301, 0, RingdownAnalysisStatus::INTERVAL_TOO_LONG) == 0);
    CHECK(check(65535, 0, RingdownAnalysisStatus::INTERVAL_TOO_LONG) == 0);
    // An interval out of the limits keeps the last valid interval
    CHECK(check(301, 250, RingdownAnalysisStatus::INTERVAL_TOO_LONG) == 250);
}

TEST_CASE("indx_head ringdown: the interval change limit is 20 %", "[indx_head][ringdown]") {
    auto check = [](uint16_t interval, uint16_t last) {
        RingdownAnalysis a { .status = RingdownAnalysisStatus::VALID, .interval = interval, .decay = 0.1f, .nozzle_presence = NozzlePresence::present, .time = 0 };
        const bool ok = ringdown_analysis_sanity_check(a, last);
        return std::pair { ok ? RingdownAnalysisStatus::VALID : a.status, last };
    };
    CHECK(check(240, 200) == std::pair { RingdownAnalysisStatus::VALID, uint16_t { 240 } });
    CHECK(check(160 + 10, 200) == std::pair { RingdownAnalysisStatus::VALID, uint16_t { 170 } });
    // 20.5 %: rejected, and the reference is cleared
    CHECK(check(241, 200) == std::pair { RingdownAnalysisStatus::INTERVAL_CHANGE_TOO_BIG, uint16_t { 0 } });
    CHECK(check(179, 225) == std::pair { RingdownAnalysisStatus::INTERVAL_CHANGE_TOO_BIG, uint16_t { 0 } });
}

TEST_CASE("indx_head ringdown: a rejected change disables the next check", "[indx_head][ringdown]") {
    // Current behavior: after one rejected measurement, the next one is compared with nothing
    uint16_t last = 200;
    RingdownAnalysis a { .status = RingdownAnalysisStatus::VALID, .interval = 290, .decay = 0.1f, .nozzle_presence = NozzlePresence::present, .time = 0 };
    CHECK_FALSE(ringdown_analysis_sanity_check(a, last));
    RingdownAnalysis b = a;
    b.status = RingdownAnalysisStatus::VALID;
    CHECK(ringdown_analysis_sanity_check(b, last));
    CHECK(last == 290);
}

TEST_CASE("indx_head ringdown: measurement period", "[indx_head][ringdown]") {
    RingdownAnalysis a { .status = RingdownAnalysisStatus::VALID, .interval = 240, .decay = 0.1f, .nozzle_presence = NozzlePresence::present, .time = 1000 };
    CHECK_FALSE(should_measure(a, 1100, true));
    CHECK(should_measure(a, 1101, true));
    CHECK_FALSE(should_measure(a, 1500, false));
    CHECK(should_measure(a, 1501, false));
    // The tick counter wraps
    a.time = 0xFFFF'FF00;
    CHECK(should_measure(a, 0x10, true));
    CHECK_FALSE(should_measure(a, 0xFFFF'FF10, true));
    // Without a valid analysis, measure each time
    a.status = GENERATE(RingdownAnalysisStatus::NOT_ENOUGH_PEAKS, RingdownAnalysisStatus::INTERVAL_TOO_SHORT, RingdownAnalysisStatus::INTERVAL_TOO_LONG, RingdownAnalysisStatus::INTERVAL_CHANGE_TOO_BIG);
    CHECK(should_measure(a, a.time, false));
}
