#pragma once
#include <module/motion.h>
void set_bed_leveling_enabled(bool enable = true);
/// Mesh values, used by PrusaToolChangerUtils::get_mbl_z_lift_height()
float fake_z_value(uint8_t x, uint8_t y);
#define Z_VALUES(X, Y) fake_z_value(X, Y)
