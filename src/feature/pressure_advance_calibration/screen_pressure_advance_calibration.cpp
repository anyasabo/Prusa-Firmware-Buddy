/// @file
#include "screen_pressure_advance_calibration.hpp"

#include "pressure_advance_calibration.hpp"

#include <array>
#include <cstdio>

#include <fsm/pressure_advance_calibration_phases.hpp>
#include <i18n.h>
#include <img_resources.hpp>
#include <standard_frame/frame_extensions/with_footer.hpp>
#include <standard_frame/frame_progress_prompt.hpp>
#include <standard_frame/frame_prompt.hpp>

using Phase = PhasePressureAdvanceCalibration;
using pressure_advance_calibration::Error;

namespace {

constexpr const char *txt_header = N_("PRESSURE ADVANCE");
constexpr const char *txt_title = N_("Pressure advance calibration");

constexpr const char *txt_intro = N_("The printer measures how fast the nozzle pressure follows changes of the flow, using the load cell. "
                                     "It extrudes about 15 mm of filament at the purge position.\n\n"
                                     "Make sure the filament is loaded and nothing blocks the nozzle.");
constexpr const char *txt_preparing = N_("Moving to the purge position and heating the nozzle.");
constexpr const char *txt_measuring = N_("Measuring, please wait.");
constexpr const char *txt_result_title = N_("Calibration done");
constexpr const char *txt_result = N_("Pressure advance: %s\n\n"
                                      "Save it for the loaded filament? It is used until the filament is unloaded, "
                                      "instead of the value from G-code (unless overriding is allowed in the Filament menu).");
constexpr const char *txt_failed_title = N_("Calibration failed");

const char *error_text(Error error) {
    switch (error) {
    case Error::no_filament:
        return N_("No filament is loaded. Load the filament and try again.");
    case Error::flexible_filament:
        return N_("Flexible filaments are not supported.");
    case Error::heating_failed:
        return N_("The nozzle did not reach the print temperature.");
    case Error::measurement_failed:
        return N_("The measurement did not complete. Please try again.");
    case Error::no_signal:
        return N_("The load cell does not register the extrusion. Make sure the filament is loaded and flows freely from the nozzle.");
    case Error::inconsistent:
        return N_("The measurements do not agree. Make sure the filament flows freely and nothing touches the nozzle, then try again.");
    case Error::too_slow:
        return N_("The nozzle pressure settles too slowly for this test. The filament may be too soft or the temperature too low.");
    case Error::out_of_range:
        return N_("The measured value is outside of the expected range.");
    case Error::none:
    case Error::aborted:
        break;
    }
    return "";
}

constexpr FooterLine::IdArray footer_items = { footer::Item::nozzle };

class FrameIntro final : public FramePrompt {
public:
    explicit FrameIntro(window_frame_t *parent)
        : FramePrompt(parent, Phase::intro, _(txt_title), _(txt_intro)) {}
};

class FramePreparing : public FramePrompt {
public:
    explicit FramePreparing(window_frame_t *parent)
        : FramePrompt(parent, Phase::preparing, _(txt_title), _(txt_preparing)) {}
};

class FrameMeasuring : public FrameProgressPrompt {
public:
    explicit FrameMeasuring(window_frame_t *parent)
        : FrameProgressPrompt(parent, Phase::measuring, _(txt_title), _(txt_measuring)) {}

    void update(const fsm::PhaseData &raw) {
        const auto data = fsm::deserialize_data<FSMPressureAdvanceCalibrationData>(raw);
        progress_bar.set_progress_percent(data.progress_percent);
    }
};

class FrameResult final : public FramePrompt {
public:
    explicit FrameResult(window_frame_t *parent)
        : FramePrompt(parent, Phase::result, _(txt_result_title), string_view_utf8::MakeNULLSTR()) {}

    void update(const fsm::PhaseData &raw) {
        const auto data = fsm::deserialize_data<FSMPressureAdvanceCalibrationData>(raw);
        snprintf(value.data(), value.size(), "%u.%03u", data.pressure_advance_100us / 10000u, (data.pressure_advance_100us % 10000u) / 10u);
        info.SetText(_(txt_result).formatted(params, value.data()));
    }

private:
    std::array<char, 8> value {};
    StringViewUtf8Parameters<16> params;
};

class FrameFailed final : public FramePrompt {
public:
    explicit FrameFailed(window_frame_t *parent)
        : FramePrompt(parent, Phase::failed, _(txt_failed_title), string_view_utf8::MakeNULLSTR()) {}

    void update(const fsm::PhaseData &raw) {
        const auto data = fsm::deserialize_data<FSMPressureAdvanceCalibrationData>(raw);
        info.SetText(_(error_text(static_cast<Error>(data.error))));
    }
};

using Frames = FrameDefinitionList<ScreenPressureAdvanceCalibration::FrameStorage,
    FrameDefinition<Phase::intro, FrameIntro>,
    FrameDefinition<Phase::preparing, WithFooter<FramePreparing, footer_items>>,
    FrameDefinition<Phase::measuring, WithFooter<FrameMeasuring, footer_items>>,
    FrameDefinition<Phase::result, FrameResult>,
    FrameDefinition<Phase::failed, FrameFailed>>;

Phase get_phase(const fsm::BaseData &fsm_base_data) {
    return GetEnumFromPhaseIndex<Phase>(fsm_base_data.GetPhase());
}

} // namespace

ScreenPressureAdvanceCalibration::ScreenPressureAdvanceCalibration()
    : ScreenFSM(txt_header) {
    header.SetIcon(&img::spool_white_16x16);
    create_frame();
}

ScreenPressureAdvanceCalibration::~ScreenPressureAdvanceCalibration() {
    destroy_frame();
}

void ScreenPressureAdvanceCalibration::create_frame() {
    Frames::create_frame(frame_storage, get_phase(fsm_base_data), &inner_frame);
}

void ScreenPressureAdvanceCalibration::destroy_frame() {
    Frames::destroy_frame(frame_storage, get_phase(fsm_base_data));
}

void ScreenPressureAdvanceCalibration::update_frame() {
    Frames::update_frame(frame_storage, get_phase(fsm_base_data), fsm_base_data.GetData());
}
