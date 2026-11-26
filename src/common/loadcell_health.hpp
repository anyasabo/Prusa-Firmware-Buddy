/// @file
#pragma once

#include <atomic>
#include <cstdint>

namespace buddy {

/// Health checks of the load cell sample stream.
///
/// It finds two faults:
/// - no_samples: no valid sample for more than sample_timeout_us. Example: the cable is off.
///   Then the HX717 data line stays high, no interrupt occurs and no sample arrives.
/// - stuck_value: the raw value does not change, or stays at an ADC rail, for more than
///   stuck_timeout_us. Real HX717 noise at gain 128 is some hundred counts, so the raw value
///   never stays constant (upstream issue #3476: a disconnected MK4 load cell read -26570).
///
/// feed() runs in the sample ISR and only increments counters, so the ISR cost stays small.
/// check() runs in one task and measures with its own clock how long the counters stayed the
/// same. Thus the ISR timestamps are not used: a sample timestamp that is a bit late, a task that
/// blocks, or a wrap of the 32-bit microsecond clock cannot cause a false fault.
/// The caller must call check() at least once each 71 minutes (the wrap period of the clock).
class LoadcellHealth {
public:
    enum class Fault : uint8_t {
        none,
        no_samples,
        stuck_value,
    };

    /// HX717 output at saturation (24-bit two's complement)
    static constexpr int32_t rail_min = -0x80'0000;
    static constexpr int32_t rail_max = 0x7F'FFFF;

    /// The HX717 gives a sample each 3.1 ms (320 Hz). The load cell channel gets 12 of 13 samples.
    /// The probe safety check stops a probe if a sample is older than 100 ms
    /// (Loadcell::MAX_LOADCELL_DATA_AGE_US). This limit is 5 times that.
    static constexpr uint32_t sample_timeout_us = 500'000;

    /// 2 s is approximately 600 samples. Recorded probe data has a noise of 178 counts RMS and no two
    /// equal consecutive samples (tests/unit/common/probe_analysis/probes).
    static constexpr uint32_t stuck_timeout_us = 2'000'000;

    /// Record a valid raw sample. Call only from the sample ISR (one writer).
    void feed(int32_t raw) {
        valid_count.store(valid_count.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        if (raw != last_raw.load(std::memory_order_relaxed) && raw != rail_min && raw != rail_max) {
            change_count.store(change_count.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        }
        last_raw.store(raw, std::memory_order_relaxed);
    }

    /// Evaluate the stream. Call periodically from one task.
    /// The first call starts the time limits.
    Fault check(uint32_t now_us) {
        const uint32_t valid = valid_count.load(std::memory_order_relaxed);
        const uint32_t change = change_count.load(std::memory_order_relaxed);

        if (!started || valid != seen_valid) {
            seen_valid = valid;
            valid_since_us = now_us;
        }
        if (!started || change != seen_change) {
            seen_change = change;
            change_since_us = now_us;
        }
        started = true;

        if (now_us - valid_since_us > sample_timeout_us) {
            return Fault::no_samples;
        }
        if (now_us - change_since_us > stuck_timeout_us) {
            return Fault::stuck_value;
        }
        return Fault::none;
    }

    /// Last raw value given to feed(), for logs
    int32_t get_last_raw() const { return last_raw.load(std::memory_order_relaxed); }

private:
    // Written by the ISR
    std::atomic<uint32_t> valid_count { 0 };
    std::atomic<uint32_t> change_count { 0 };
    static_assert(std::atomic<uint32_t>::is_always_lock_free, "Lock free type must be used from ISR.");
    static_assert(std::atomic<int32_t>::is_always_lock_free, "Lock free type must be used from ISR.");
    std::atomic<int32_t> last_raw { 0 };

    // Used only by check()
    bool started { false };
    uint32_t seen_valid { 0 };
    uint32_t seen_change { 0 };
    uint32_t valid_since_us { 0 };
    uint32_t change_since_us { 0 };
};

} // namespace buddy
