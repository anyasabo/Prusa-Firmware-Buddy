/// @file
#pragma once

#include <atomic>
#include <cstdint>

namespace buddy {

/// Health checks of the load cell sample stream.
///
/// It finds two faults. Each fault has the signatures that it catches:
/// - no_samples: no valid sample for more than sample_timeout_us.
///   Signature: the cable between the HX717 and the board is off. The pull-up holds DOUT high,
///   so no interrupt occurs and no sample arrives. Also a dead HX717, or a stream of failed reads.
/// - implausible_value: for more than implausible_timeout_us, no sample that is plausible and
///   different from the previous sample. A sample is plausible if abs(raw) < implausible_abs and
///   the caller tells that its tared load is plausible.
///   Signatures:
///   - The wires between the bridge and the HX717 separate during a print. Measured on a CORE One L:
///     the raw value went from about -1.1M counts (-21 kg untared) to about -4500 counts with
///     the normal noise. After the tare, this is a load of about +21 kg (caller check).
///   - A constant raw value (upstream issue #3476 asks for this check), a value at an ADC rail,
///     or a value near a rail that still changes.
///   Not caught: a separation before the tare. The tare then hides the offset, and the open input
///   gives noise like a bridge. Only a stored reference of the untared value can catch that.
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
        implausible_value,
    };

    /// HX717 output at saturation (24-bit two's complement)
    static constexpr int32_t rail_min = -0x80'0000;
    static constexpr int32_t rail_max = 0x7F'FFFF;

    /// Half of the full scale: 4194304 counts, approximately 80 kg at the default scale of 0.0192 g per count.
    /// On the CORE One L of the owner, the untared value of a connected load cell was -20.7 kg to -23.1 kg
    /// (about -1.1M counts) in the probe log of 2026-11-22. In upstream issue #3476, a connected MK4 showed
    /// about 15270 g untared (about 795k counts). The bridge offset differs between printers, so only a
    /// wide band is safe. The band catches a saturated input, not a separated bridge (that reads near zero).
    static constexpr int32_t implausible_abs = 1 << 22;

    /// The HX717 gives a sample each 3.1 ms (320 Hz). The load cell channel gets 12 of 13 samples.
    /// The probe safety check stops a probe if a sample is older than 100 ms
    /// (Loadcell::MAX_LOADCELL_DATA_AGE_US). This limit is 5 times that.
    static constexpr uint32_t sample_timeout_us = 500'000;

    /// 2 s is approximately 600 samples. Recorded probe data has a noise of 178 counts RMS and no two
    /// equal consecutive samples (tests/unit/common/probe_analysis/probes).
    static constexpr uint32_t implausible_timeout_us = 2'000'000;

    /// Record a valid raw sample. Call only from the sample ISR (one writer).
    /// Outside high precision mode and after a tare, a larger tared load [g] is not physical.
    /// The self test accepts a tap of 500 to 2000 g. FailureOnLoadAboveEnforcer uses 3000 g during probe moves.
    /// The nozzle cooling from 170 to 35 degC moved the load by about -570 g on a CORE One L.
    /// A separation of the bridge wires on a CORE One L gave about +21 kg.
    static constexpr float max_plausible_load_g = 5000.f;

    /// @param tared_load_g Load relative to the last tare
    /// @param high_precision True during a probe: a probe can push harder for a short time
    /// @param tare_valid False before the first tare: then the offset is 0, and a connected load cell
    ///                   can read -21 kg untared
    static constexpr bool load_plausible(float tared_load_g, bool high_precision, bool tare_valid) {
        return high_precision || !tare_valid || (tared_load_g <= max_plausible_load_g && tared_load_g >= -max_plausible_load_g);
    }

    /// @param load_plausible False if the caller knows that the load cannot be physical
    void feed(int32_t raw, bool load_plausible) {
        valid_count.store(valid_count.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        // The rails are outside the plausible band
        static_assert(rail_max >= implausible_abs && rail_min <= -implausible_abs);
        if (load_plausible && raw != last_raw.load(std::memory_order_relaxed) && raw < implausible_abs && raw > -implausible_abs) {
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
        if (now_us - change_since_us > implausible_timeout_us) {
            return Fault::implausible_value;
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
