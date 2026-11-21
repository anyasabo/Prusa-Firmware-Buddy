/// @file
/// Order in which G29 probes the major points of the UBL mesh (probe_major_points() in ubl_G29.cpp).
/// No Marlin dependency, so the host unit tests can check it.
#pragma once

namespace buddy::probe {

struct GridLayout {
    int max_points_x; ///< GRID_MAX_POINTS_X
    int max_points_y; ///< GRID_MAX_POINTS_Y
    int border; ///< GRID_BORDER: rows and columns at the edge that are never probed
    int major_step; ///< GRID_MAJOR_STEP: index distance between probed points
};

/// Calls visit(x, y) with the mesh indices of each major point, in probe order: rows from the
/// highest Y index down, and the X direction alternates (serpentine), so that the head does not
/// travel back to the start of each row. The direction of a row depends on its index in the full
/// grid, not on the rows that the caller skips. visit() returns false to stop.
template <class Visit>
void for_each_major_grid_point(const GridLayout &g, Visit &&visit) {
    for (int y = g.max_points_y - g.border - 1; y >= g.border; y -= g.major_step) {
        const int y_idx = (y - g.border) / g.major_step;
        const bool odd_row = y_idx % 2 == 1;
        const int x0 = odd_row ? g.border : g.max_points_x - 1 - g.border;
        const int x_step = odd_row ? g.major_step : -g.major_step;

        for (int x = x0; g.border <= x && x < g.max_points_x - g.border; x += x_step) {
            if (!visit(x, y)) {
                return;
            }
        }
    }
}

/// True if the measured points span so much height that G29 offers Z calibration
/// (MBL_Z_DIFF_CALIB_WARNING_THRESHOLD).
constexpr bool mesh_is_uneven(float min_z, float max_z, float threshold) {
    return max_z - min_z >= threshold;
}

} // namespace buddy::probe
