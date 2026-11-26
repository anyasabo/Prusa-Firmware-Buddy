#include <catch2/catch_test_macros.hpp>
#include <loadcell_health.hpp>

using buddy::LoadcellHealth;
using Fault = LoadcellHealth::Fault;

namespace {

/// Drives a LoadcellHealth with a fake clock, like the firmware does:
/// the sample ISR feeds at 320 Hz, the default task checks every 10 ms.
struct Rig {
    LoadcellHealth health;
    uint32_t now_us;
    uint32_t noise_state = 1;

    explicit Rig(uint32_t start_us = 0)
        : now_us(start_us) {
        REQUIRE(health.check(now_us) == Fault::none);
    }

    /// A value with HX717-like noise (a few hundred counts around zero).
    int32_t noisy() {
        noise_state = noise_state * 1103515245 + 12345;
        return static_cast<int32_t>((noise_state >> 16) % 400) - 200;
    }

    /// Advance the clock by duration_us. Feed one sample each 3125 us from sample(), check each 10 ms.
    /// Returns the first fault that check() reports, or Fault::none.
    template <typename Sample>
    Fault run(uint32_t duration_us, Sample sample) {
        Fault first = Fault::none;
        uint32_t next_sample = 0;
        uint32_t next_check = 0;
        for (uint32_t t = 0; t <= duration_us; t += 125) {
            if (t >= next_sample) {
                if (auto v = sample(); v.has_value) {
                    health.feed(v.value, v.load_plausible);
                }
                next_sample += 3125;
            }
            if (t >= next_check) {
                const Fault f = health.check(now_us + t);
                if (first == Fault::none) {
                    first = f;
                }
                next_check += 10'000;
            }
        }
        now_us += duration_us;
        return first;
    }
};

struct Maybe {
    bool has_value;
    int32_t value;
    bool load_plausible = true;
};

} // namespace

TEST_CASE("loadcell_health: noisy stream has no fault") {
    Rig rig;
    CHECK(rig.run(60'000'000, [&] { return Maybe { true, rig.noisy() }; }) == Fault::none);
}

TEST_CASE("loadcell_health: stream that stops raises no_samples") {
    Rig rig;
    REQUIRE(rig.run(1'000'000, [&] { return Maybe { true, rig.noisy() }; }) == Fault::none);

    // Cable off: the HX717 data line stays high, no interrupt, no sample.
    // Not yet after 400 ms ...
    CHECK(rig.run(400'000, [] { return Maybe { false, 0 }; }) == Fault::none);
    // ... but after 500 ms and one check period.
    CHECK(rig.run(200'000, [] { return Maybe { false, 0 }; }) == Fault::no_samples);
}

TEST_CASE("loadcell_health: no sample since the start raises no_samples") {
    Rig rig;
    CHECK(rig.run(600'000, [] { return Maybe { false, 0 }; }) == Fault::no_samples);
}

TEST_CASE("loadcell_health: stream recovers after a gap") {
    Rig rig;
    REQUIRE(rig.run(600'000, [] { return Maybe { false, 0 }; }) == Fault::no_samples);
    // The first new sample clears the fault. The caller decides if a fault is fatal.
    CHECK(rig.run(1'000'000, [&] { return Maybe { true, rig.noisy() }; }) == Fault::none);
}

TEST_CASE("loadcell_health: constant raw value raises implausible_value") {
    Rig rig;
    REQUIRE(rig.run(1'000'000, [&] { return Maybe { true, rig.noisy() }; }) == Fault::none);
    // Upstream issue #3476: a disconnected MK4 load cell read a constant -26570.
    CHECK(rig.run(1'900'000, [] { return Maybe { true, -26570 }; }) == Fault::none);
    CHECK(rig.run(200'000, [] { return Maybe { true, -26570 }; }) == Fault::implausible_value);
}

TEST_CASE("loadcell_health: value at a rail raises implausible_value") {
    for (const int32_t rail : { LoadcellHealth::rail_min, LoadcellHealth::rail_max }) {
        Rig rig;
        REQUIRE(rig.run(1'000'000, [&] { return Maybe { true, rig.noisy() }; }) == Fault::none);
        // A saturated HX717 gives the rail value. The rail does not count as a change.
        CHECK(rig.run(2'100'000, [&] { return Maybe { true, rail }; }) == Fault::implausible_value);
    }
}

TEST_CASE("loadcell_health: change between the two rails raises implausible_value") {
    Rig rig;
    bool flip = false;
    CHECK(rig.run(2'100'000, [&] {
        flip = !flip;
        return Maybe { true, flip ? LoadcellHealth::rail_min : LoadcellHealth::rail_max };
    }) == Fault::implausible_value);
}

TEST_CASE("loadcell_health: blocked checker does not raise a false fault") {
    Rig rig;
    REQUIRE(rig.run(1'000'000, [&] { return Maybe { true, rig.noisy() }; }) == Fault::none);
    // The default task blocks for 5 s, the ISR keeps feeding.
    for (int i = 0; i < 1600; ++i) {
        rig.health.feed(rig.noisy(), true);
    }
    rig.now_us += 5'000'000;
    CHECK(rig.health.check(rig.now_us) == Fault::none);
}

TEST_CASE("loadcell_health: microsecond clock wrap does not raise a false fault") {
    Rig rig(UINT32_MAX - 1'000'000);
    CHECK(rig.run(3'000'000, [&] { return Maybe { true, rig.noisy() }; }) == Fault::none);
}

TEST_CASE("loadcell_health: microsecond clock wrap does not hide a dead stream") {
    Rig rig(UINT32_MAX - 300'000);
    CHECK(rig.run(700'000, [] { return Maybe { false, 0 }; }) == Fault::no_samples);
}

TEST_CASE("loadcell_health: noisy value near a rail raises implausible_value") {
    // A floating bridge input can drift to a high value and still change.
    // Such a value is not plausible for a connected load cell.
    for (const int32_t base : { LoadcellHealth::rail_max - 100'000, LoadcellHealth::rail_min + 100'000, LoadcellHealth::implausible_abs }) {
        CAPTURE(base);
        Rig rig;
        REQUIRE(rig.run(1'000'000, [&] { return Maybe { true, rig.noisy() }; }) == Fault::none);
        CHECK(rig.run(2'100'000, [&] { return Maybe { true, base + (base > 0 ? 1 : -1) * (rig.noisy() + 200) }; }) == Fault::implausible_value);
    }
}

TEST_CASE("loadcell_health: noisy value with a large plausible offset has no fault") {
    // Upstream issue #3476: a connected MK4 load cell read about 15270 g untared (approximately 795k counts).
    for (const int32_t base : { 795'000, -1'400'000, LoadcellHealth::implausible_abs - 1000 }) {
        CAPTURE(base);
        Rig rig;
        CHECK(rig.run(10'000'000, [&] { return Maybe { true, base + rig.noisy() }; }) == Fault::none);
    }
}

TEST_CASE("loadcell_health: noisy value with an implausible tared load raises implausible_value") {
    // CORE One L of the owner, 2026-11-26: the wires between the bridge and the HX717 separated.
    // The raw value went from about -1.1M counts (-21 kg untared) to -4498 counts with a noise of 208 counts
    // (lc/lcraw.csv, 10054 samples). Only 18 of 10053 consecutive pairs were equal. After a tare at -21 kg,
    // the tared load is about +21 kg. The caller tells that this load is not plausible.
    Rig rig;
    REQUIRE(rig.run(1'000'000, [&] { return Maybe { true, rig.noisy() - 1'100'000 }; }) == Fault::none);
    CHECK(rig.run(1'900'000, [&] { return Maybe { true, rig.noisy() - 4498, false }; }) == Fault::none);
    CHECK(rig.run(200'000, [&] { return Maybe { true, rig.noisy() - 4498, false }; }) == Fault::implausible_value);
}

TEST_CASE("loadcell_health: a short implausible load has no fault") {
    Rig rig;
    for (int i = 0; i < 10; ++i) {
        // 1.5 s of an implausible load (for example a nozzle crash), then 0.5 s of a plausible load
        CHECK(rig.run(1'500'000, [&] { return Maybe { true, rig.noisy(), false }; }) == Fault::none);
        CHECK(rig.run(500'000, [&] { return Maybe { true, rig.noisy() }; }) == Fault::none);
    }
}

TEST_CASE("loadcell_health: load_plausible") {
    using LH = LoadcellHealth;
    // After a tare, outside high precision mode: the limit applies to both signs
    CHECK(LH::load_plausible(0.f, false, true));
    CHECK(LH::load_plausible(LH::max_plausible_load_g, false, true));
    CHECK(LH::load_plausible(-LH::max_plausible_load_g, false, true));
    CHECK_FALSE(LH::load_plausible(LH::max_plausible_load_g + 1.f, false, true));
    CHECK_FALSE(LH::load_plausible(-LH::max_plausible_load_g - 1.f, false, true));
    // Separated bridge wires on the CORE One L: tare at -21 kg, then the input reads about -86 g
    CHECK_FALSE(LH::load_plausible(21'000.f, false, true));
    // Upstream issue #3476 (MK4): connected about +15270 g untared, disconnected about -26570 g
    CHECK_FALSE(LH::load_plausible(-26'570.f - 15'270.f, false, true));
    // A probe (high precision mode) can push harder for a short time
    CHECK(LH::load_plausible(21'000.f, true, true));
    // Before the first tare, the offset is 0: the untared value of a connected load cell can be -21 kg
    CHECK(LH::load_plausible(-21'000.f, false, false));
}
