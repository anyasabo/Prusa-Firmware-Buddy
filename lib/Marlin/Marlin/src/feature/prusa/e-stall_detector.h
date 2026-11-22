#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <algorithm>

#include <utils/leaky_bucket.hpp>

class MotorStallFilter {
public:
    constexpr MotorStallFilter() = default;
    ~MotorStallFilter() = default;

    bool ProcessSample(int32_t value);

    void SetDetectionThreshold(float dt) {
        detectionThreshold.store(dt, std::memory_order_relaxed);
    }

    constexpr float DetectionThreshold() const { return detectionThreshold.load(std::memory_order_relaxed); }

    /// The filter output of the last sample (compared with the detection threshold).
    float LastFiltered() const { return lastFiltered; }

#ifndef UNITTEST
private:
#endif
    std::array<int32_t, 5> buffer {}; // Buffer to store previous input values
    std::atomic<float> detectionThreshold = 700'000.F;
    float lastFiltered = 0.F;
#ifdef UNITTEST
    float filteredValue; // for unit tests, filteredValue is a member of the class allowing better debugging experience
#endif
};

// Note about thread safety.
//
// We read the reports from the default (Marlin) task, but feed the samples
// from some other place (interrupt or puppy task). Therefore, we need to deal with thread safety.
//
// The filter and bucket "live" inside the place where we feed samples. We
// communicate by setting atomic flags. Clearing of both the filter and the
// bucket happens through flags too.
class EMotorStallDetector {
    friend class BlockEStallDetection;
    friend class EStallDetectionStateLatch;

public:
    /// Singleton instance of the E-motor stall detector
    /// Technically, it is not required to have exactly one instance of this class in the firmware,
    /// but it makes it simpler to use.
    static EMotorStallDetector &Instance() {
        static EMotorStallDetector emsd;
        return emsd;
    }

    /// Record and process incoming raw readings from the LoadCell
    void ProcessSample(int32_t value, uint32_t time_us);

    /// What the filter saw since the last TakeObservation(), for diagnostics (metric e_stall).
    struct Observation {
        float peak; ///< highest filter output (0 if no sample or none above 0)
        uint32_t over_threshold; ///< samples over the detection threshold
        uint32_t samples; ///< samples processed
    };

    /// @returns the observation since the last call, and starts a new interval.
    /// Call it from one task only (the Marlin task). The samples come from the load cell sample
    /// path (the HX717 interrupt; on the XL the puppy task).
    Observation TakeObservation() {
        return {
            .peak = obs_peak.exchange(0.F, std::memory_order_relaxed),
            .over_threshold = obs_over.exchange(0, std::memory_order_relaxed),
            .samples = obs_samples.exchange(0, std::memory_order_relaxed),
        };
    }

    /// @returns raw detected flag disregarding blocked and enabled flags
    constexpr bool DetectedRaw() const { return detected.load(std::memory_order_relaxed); }

    /// @returns true if a stall has been detected somewhere in the past.
    /// The idea is to keep the filter running almost asynchronnously in the back and ask for the @ref detected flag occasionally.
    /// When the flag has been read by the caller code, it may clear it in order to get a new detected == true in the future.
    constexpr bool DetectedUnreported() const {
        return detected_many.load(std::memory_order_relaxed) && (!reported) && (blocked <= 0) && enabled;
    }

    inline void ClearDetected() {
        detected.store(false, std::memory_order_relaxed);
        detected_many.store(false, std::memory_order_relaxed);
        reported = false;
        clear_bucket.store(true, std::memory_order_relaxed);
    }

    inline bool Reported() const {
        return reported;
    }

    inline void ClearReported() {
        reported = false;
    }

    int Blocked() const { return blocked; }

    constexpr bool Enabled() const {
        return enabled;
    }

    void SetEnabled(bool set = true);

    /// Performs evaluation of various runtime conditions.
    /// When a stall is detected multiple times, sets \p reported to true (even if not enabled)
    /// @returns true when the preceding sequence of load cell data events is considered as a stalled E-motor.
    bool Evaluate(bool movingE, bool directionE);

    void SetDetectionThreshold(float dt) {
        emf.SetDetectionThreshold(dt);
    }

    /// Set ignore parameters.
    ///
    /// @param count Ignore first count events in short batch (report the count + 1).
    /// @param forget_after_ms Forget each unreported one after this amount of ms.
    ///
    /// As an expample, if we set count = 2, forget_after_ms = 500
    /// - If first there are two detections that happen at the same time (for
    ///   simplicity), nothing is reported yet.
    /// - If 500ms elapses, one of them is forgotten (it would take another
    ///   500ms to forget the other one).
    /// - Another detection happens - nothing reported yet (as we have
    ///   forgotten one of the previous ones).
    /// - Yet another detection happens after 100ms (not enough time to forget
    ///   anything yet) -> detection is reported (accumulated 3 events).
    void SetIgnore(uint32_t count, uint32_t forget_after_ms) {
        // The bucket operates in us, because of the sample timer resolution.
        bucket.set_parameters(count, forget_after_ms * 1000);
    }
    constexpr float DetectionThreshold() const { return emf.DetectionThreshold(); }

#ifndef UNITTEST
private:
#endif
    constexpr EMotorStallDetector()
        // Report when there are at least 2 skips in 2 seconds
        // (1 of them "fit" without any report, it takes 2000ms to "forget" each one).
        : bucket(2, 2000000) {}
    ~EMotorStallDetector() = default;

    MotorStallFilter emf;
    /// Used to postpone reporting after X detected spikes in a short time.
    ///
    /// To avoid overzealous reporting, this can be used to fine-tune reporting
    /// to do so only after X detected spikes in a short time.
    LeakyBucket bucket;

    /// Used to prevent the filter from generating further detected events even if they were real - e.g. while doing a toolchange.
    /// Increased/decreased by each blocker. >0 = blocked. <0 should never happen
    int blocked = 0;

    // Diagnostics, see TakeObservation(). A race between a new peak and the exchange can put one
    // sample's value into the next interval. That is fine for a diagnostic metric.
    std::atomic<float> obs_peak = 0.F;
    std::atomic<uint32_t> obs_over = 0;
    std::atomic<uint32_t> obs_samples = 0;

    /// true if filter value exceeded the threshold
    std::atomic<bool> detected = false;

    /// true if multiple detections (based on the bucket above) happened.
    std::atomic<bool> detected_many = false;

    /// true if we requested clearing of bucket.
    ///
    /// Because the request comes from a different thread than in which the bucket lives.
    std::atomic<bool> clear_bucket = false;

    /// Set to true when \ref Evaluate returns true.
    bool reported : 1 = false;

    /// Enables/Disables the whole feature - used to control the filter from the UI
    /// Disabled by default, the outer code must enable the filter first (probably after reading a flag from EEPROM)
    bool enabled : 1 = false;
};

class BlockEStallDetection {
public:
    BlockEStallDetection() {
        auto &emsd = EMotorStallDetector::Instance();
        emsd.blocked++;
        emsd.detected.store(false, std::memory_order_relaxed);
        emsd.detected_many.store(false, std::memory_order_relaxed);
    }
    ~BlockEStallDetection() {
        auto &emsd = EMotorStallDetector::Instance();
        emsd.blocked--;
        if (emsd.blocked == 0) {
            emsd.clear_bucket.store(true, std::memory_order_relaxed);
        }
    }
};

class EStallDetectionStateLatch {
    float detectionThreshold;
    bool enabled;

public:
    EStallDetectionStateLatch() {
        auto &emsd = EMotorStallDetector::Instance();
        enabled = emsd.enabled;
        detectionThreshold = emsd.DetectionThreshold();
    }
    ~EStallDetectionStateLatch() {
        auto &emsd = EMotorStallDetector::Instance();
        emsd.SetEnabled(enabled);
        emsd.ClearDetected(); // disregard everything which happened during the state latch being active
        emsd.SetDetectionThreshold(detectionThreshold);
    }
};
