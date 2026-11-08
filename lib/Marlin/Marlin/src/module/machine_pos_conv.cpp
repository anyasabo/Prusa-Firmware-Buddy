/// @file
#include <module/motion.h>
#include <module/planner.h>

#include <algorithm>

#if HAS_LEVELING
    #include <feature/bedlevel/bedlevel.h>
#endif

// This unit has UNITTESTS in machine_pos_conv_tests.cpp

// to_machine_pos() and to_native_pos() are the Buddy versions of Planner::apply_modifiers() and
// Planner::unapply_modifiers() in upstream Marlin, with the same order:
// - to_machine_pos(): skew, then MBL. MBL looks up the mesh at the skewed XY.
// - to_native_pos(): MBL, then unskew.
// As in Marlin, G29 probes the mesh with the skew on (probe_at_point() -> do_blocking_move_to() -> to_machine_pos()).
// As in Marlin, skew() and unskew() do nothing near the X and Y limits (COORDINATE_OKAY). In that band,
// to_native_pos() is not the exact inverse of to_machine_pos().

MachinePosXYZ to_machine_pos(const xyz_pos_t &pos) {
    MachinePosXYZ result = pos.to_tag<MachinePosTag>();

#if ENABLED(SKEW_CORRECTION)
    planner.skew(result);
#endif

#if HAS_MESH
    if (planner.leveling_active) {
        const float mbl_correction = ubl.get_z_correction(result.xy());

    #if ENABLED(ENABLE_LEVELING_FADE_HEIGHT)
        if (Planner::z_fade_height != 0.0f) {
            result.z += mbl_correction * std::clamp<float>(1 - pos.z * Planner::inverse_z_fade_height, 0, 1);
        } else
    #endif
        {
            // No fade height, apply verbatim
            result.z += mbl_correction;
        }
    }
#endif

    return result;
}

xyz_pos_t to_native_pos(const MachinePosXYZ &pos) {
    xyz_pos_t result = pos.to_tag<NativePosTag>();

#if HAS_MESH
    if (planner.leveling_active) {
        const float mbl_correction = ubl.get_z_correction(result.xy());

    #if ENABLED(ENABLE_LEVELING_FADE_HEIGHT)
        if (Planner::z_fade_height != 0) {
            if (pos.z >= Planner::z_fade_height) {
                // Above fade height - correction does not apply

            } else if (mbl_correction >= Planner::z_fade_height) {
                // The function stops being monotonous and math breaks down. Shouldn't happen in practice.

            } else if (pos.z < mbl_correction) {
                // Full correction applied - mirroring the clamp in to_machine_pos
                result.z = pos.z - mbl_correction;

            } else {
                // machine_z = native_z + mbl_correction * fade(native_z)
                // fade(z) = 1 - native_z/fade_height
                //
                // So:
                // - machine_z = native_z + mbl_correction * (1 - native_z / fade_height)
                // - machine_z = native_z + mbl_correction - mbl_correction * native_z / fade_height
                // - machine_z = native_z * (1 - mbl_correction / fade_height) + mbl_correction
                //
                // Solving for native_z:
                //   native_z = (machine_z - mbl_correction) / (1 - mbl_correction / fade_height)
                result.z = (pos.z - mbl_correction) / (1.0f - mbl_correction * Planner::inverse_z_fade_height);
            }
        } else
    #endif
        {
            // No fade
            result.z -= mbl_correction;
        }
    }
#endif

#if ENABLED(SKEW_CORRECTION)
    planner.unskew(result);
#endif

    return result;
}
