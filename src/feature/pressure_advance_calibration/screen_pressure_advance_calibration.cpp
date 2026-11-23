/// @file
#include "screen_pressure_advance_calibration.hpp"

#include "pressure_advance_calibration.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include <fsm/pressure_advance_calibration_phases.hpp>
#include <gui/auto_layout.hpp>
#include <gui/event/focus_event.hpp>
#include <gui/event/gui_event.hpp>
#include <gui/event/knob_event.hpp>
#include <i18n.h>
#include <img_resources.hpp>
#include <marlin_client.hpp>
#include <numeric_input_config_common.hpp>
#include <radio_button.hpp>
#include <sound.hpp>
#include <window_text.hpp>
#include <standard_frame/frame_extensions/with_footer.hpp>
#include <standard_frame/frame_progress_prompt.hpp>
#include <standard_frame/frame_prompt.hpp>

#include <option/has_touch.h>
#if HAS_TOUCH()
    #include <dialog_numeric_input.hpp>
#endif

using Phase = PhasePressureAdvanceCalibration;
using pressure_advance_calibration::Error;

namespace {

constexpr const char *txt_header = N_("PRESSURE ADVANCE");
constexpr const char *txt_title = N_("Pressure advance calibration");

constexpr const char *txt_intro = N_("The load cell measures how quickly the nozzle pressure follows flow changes. "
                                     "About 20 mm of filament is extruded at the purge position.\n\n"
                                     "Remove any purged filament from the nozzle first.");
constexpr const char *txt_temperature_title = N_("Calibration temperature");
constexpr const char *txt_temperature = N_("The pressure advance depends on the nozzle temperature. Set the one you print this filament at.");
constexpr const char *txt_nozzle_temperature = N_("Nozzle Temperature");
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
        return N_("The nozzle did not reach the calibration temperature.");
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

/// Nozzle temperature field. While focused, the knob changes the value; a click moves on to the buttons.
/// Touching it opens the numeric keypad.
class WindowNozzleTemperature final : public window_text_t {
public:
    WindowNozzleTemperature(window_t *parent, const NumericInputConfig &config, window_t &next)
        : window_text_t(parent, {}, is_multiline::no, is_closed_on_click_t::no)
        , config(config)
        , next(next) {
        set_font(GuiDefaults::FontBig);
        SetAlignment(Align_t::Center());
        Enable();
    }

    int16_t value() const {
        return value_;
    }

    void set_value(float value) {
        value_ = static_cast<int16_t>(std::clamp(std::round(value), config.min_value, config.max_value));
        snprintf(text.data(), text.size(), "%d °C", value_);
        SetText(string_view_utf8::MakeRAM(text.data()));
        Invalidate();
    }

protected:
    void windowEvent(window_t *sender, GUI_event_t event, void *param) override {
        switch (event) {

        case GUI_event_t::KNOB: {
            auto &ctx = *static_cast<GuiEventContext *>(param);
            const int16_t old_value = value_;
            set_value(value_ + ctx.event.value<gui_event::KnobEvent>().diff * config.step);
            sound::play(value_ != old_value ? SoundType::encoder_move : SoundType::blind_alert);
            // Also at the limits, so that only a click leaves the field
            ctx.accept();
            break;
        }

        case GUI_event_t::CLICK:
            next.SetFocus(gui_event::FocusInEvent::Reason::forward_focus_chain);
            break;

#if HAS_TOUCH()
        case GUI_event_t::TOUCH_CLICK:
            if (const auto result = DialogNumericInput::exec(_(txt_nozzle_temperature), value_, config)) {
                set_value(static_cast<float>(*result));
            }
            break;
#endif

        default:
            window_text_t::windowEvent(sender, event, param);
            break;
        }
    }

private:
    const NumericInputConfig &config;
    window_t &next;
    std::array<char, 12> text {};
    int16_t value_ = 0;
};

class FrameSelectTemperature {
public:
    explicit FrameSelectTemperature(window_frame_t *parent)
        : title(parent, {}, is_multiline::no, is_closed_on_click_t::no, _(txt_temperature_title))
        , title_line(parent, {})
        , info(parent, {}, is_multiline::yes, is_closed_on_click_t::no, _(txt_temperature))
        , temperature(parent, temperature_config, radio)
        , radio(parent, {}, RadioButton::Responses_t { Response::Continue, Response::Abort, Response::_none, Response::_none }) {
        title.set_font(GuiDefaults::FontBig);
        title.SetTextColor(COLOR_BRAND);
        title.SetAlignment(Align_t::LeftBottom());
        title_line.SetBackColor(COLOR_DARK_GRAY);
        info.SetAlignment(Align_t::LeftTop());

        radio.set_callback([this](Response response) {
            if (response == Response::Continue) {
                marlin_client::FSM_response_variant(Phase::select_temperature, FSMResponseVariant::make<int16_t>(temperature.value()));
            } else {
                marlin_client::FSM_response(Phase::select_temperature, response);
            }
        });

        static constexpr std::array layout {
            StackLayoutItem { .height = 32, .margin_side = 16, .margin_bottom = 4 },
            StackLayoutItem { .height = 2, .margin_side = 16 },
            StackLayoutItem { .height = StackLayoutItem::stretch, .margin_side = 16, .margin_top = 16 },
            StackLayoutItem { .height = 40, .width = 160, .margin_bottom = 16 },
            standard_stack_layout::for_radio,
        };
        std::array<window_t *, layout.size()> windows { &title, &title_line, &info, &temperature, &radio };
        layout_vertical_stack(parent->GetRect(), windows, layout);

        // Nothing is captured: the knob moves between the field and the buttons. The buttons show no
        // selection until they get the focus.
        radio.SetFocus();
        temperature.SetFocus();
    }

    void update(const fsm::PhaseData &raw) {
        // Only the initial proposal, the user may have changed the value since
        if (!proposed) {
            temperature.set_value(fsm::deserialize_data<FSMPressureAdvanceCalibrationTemperature>(raw).nozzle_temperature);
            proposed = true;
        }
    }

private:
    const NumericInputConfig temperature_config = numeric_input_config::filament_nozzle_temperature(AllTools {});
    window_text_t title;
    BasicWindow title_line;
    window_text_t info;
    WindowNozzleTemperature temperature;
    RadioButton radio;
    bool proposed = false;
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
    FrameDefinition<Phase::select_temperature, FrameSelectTemperature>,
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
