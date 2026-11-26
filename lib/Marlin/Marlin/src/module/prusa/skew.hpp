/// @file
/// Buddy additions to the skew correction (see module/planner_skew.h): the config store and homing.
#pragma once

#include <inc/MarlinConfigPre.h>

#if ENABLED(SKEW_CORRECTION)

    #include <module/planner_skew.h>

/// \returns the factors from the config store, or the defaults (XY_SKEW_FACTOR, ...) if a stored factor
/// is out of SKEW_FACTOR_MIN..SKEW_FACTOR_MAX
skew_factor_t stored_skew_factor();

/// Sets planner.skew_factor to stored_skew_factor(). MarlinSettings::reset() calls it (at boot, M501, M502).
/// The first call (at boot) only sets the factors, because the planner does not run yet.
/// Later calls keep the machine position: the nozzle does not move, current_position changes.
void load_skew_factor();

/// Sets planner.skew_factor (M852) and keeps the machine position. It waits for the queued moves first.
/// If a crash or an abort empties the queue during the wait (planner.draining()), the factors do not change.
void set_skew_factor(const skew_factor_t &factor);

/// Turns the skew off (planner.skew_disabled) until the matching restore_skew(). Calls can nest.
/// The machine position stays the same. planner.skew_factor does not change, so an M852 in the meantime is kept.
/// Use it for moves along the machine axes, for example homing.
void disable_skew();

/// Ends one disable_skew(). The machine position stays the same.
void restore_skew();

/// \returns the native position that goes to the same machine position with the skew on as \p native_without_skew
/// went to with the skew off. Call it with the skew on. Power panic uses it for the positions that the AC fault ISR
/// saved during a skew guard (G28, G29, G12). As unskew(), it does nothing in the edge band.
xyz_pos_t native_pos_with_skew(const xyz_pos_t &native_without_skew);

/// disable_skew() for the lifetime of the object
class TemporarySkewDisabler {
public:
    TemporarySkewDisabler() { disable_skew(); }
    ~TemporarySkewDisabler() { restore_skew(); }

    TemporarySkewDisabler(const TemporarySkewDisabler &) = delete;
    TemporarySkewDisabler &operator=(const TemporarySkewDisabler &) = delete;
};

#endif
