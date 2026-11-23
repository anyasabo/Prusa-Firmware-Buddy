/// @file
#pragma once

#include <option/has_pressure_advance_calibration.h>

static_assert(HAS_PRESSURE_ADVANCE_CALIBRATION());

namespace PrusaGcodeSuite {

void M1572();

}
