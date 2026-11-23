/// @file
#pragma once

#include <screen_fsm.hpp>
#include <option/has_pressure_advance_calibration.h>

static_assert(HAS_PRESSURE_ADVANCE_CALIBRATION());

class ScreenPressureAdvanceCalibration final : public ScreenFSM {
public:
    ScreenPressureAdvanceCalibration();
    ~ScreenPressureAdvanceCalibration();

protected:
    void create_frame() final;
    void destroy_frame() final;
    void update_frame() final;
};
