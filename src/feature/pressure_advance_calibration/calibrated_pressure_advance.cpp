/// @file
#include "calibrated_pressure_advance.hpp"

#include <array>
#include <cstdio>

#include <Marlin/src/core/serial.h>
#include <Marlin/src/feature/pressure_advance/pressure_advance_config.hpp>
#include <Marlin/src/module/planner.h>
#include <config_store/store_instance.hpp>
#include <filament.hpp>
#include <logging/log.hpp>
#include <timing.h>

LOG_COMPONENT_REF(PressureAdvanceCalibration);

namespace pressure_advance_calibration {

namespace {

    constexpr uint32_t check_interval_ms = 250;

    /// What the effective pressure advance depends on, apart from G-code
    struct Context {
        std::optional<float> saved;
        bool override_allowed;

        bool operator==(const Context &) const = default;
    };

    /// Settings last requested by G-code
    pressure_advance::Config requested;

    /// G-code set the pressure advance since the context last changed
    bool gcode_overrides = false;

    /// Context the effective settings were last applied for
    std::optional<Context> applied;

    uint32_t last_check_ms = 0;

    Context current_context() {
        return { .saved = saved_value(), .override_allowed = config_store().pressure_advance_gcode_override.get() };
    }

    pressure_advance::Config effective(const Context &context) {
        if (context.saved && !(context.override_allowed && gcode_overrides)) {
            return { .pressure_advance = *context.saved, .smooth_time = requested.smooth_time };
        }
        return requested;
    }

    void apply(const Context &context) {
        const pressure_advance::Config config = effective(context);
        if (pressure_advance::get_axis_e_config() != config) {
            planner.synchronize();
            if (!planner.draining()) {
                pressure_advance::set_axis_e_config(config);
            }
        }
        applied = context;
    }

    /// The saved value belongs to the filament that was loaded when it was measured
    void discard_if_filament_changed() {
        if (!(config_store().calibrated_pressure_advance.get() > 0)) {
            return;
        }
        const uint8_t tool = config_store().calibrated_pressure_advance_tool.get();
        const FilamentType filament = config_store().calibrated_pressure_advance_filament.get();
        if (tool < VirtualToolIndex::count && FilamentType::for_tool(VirtualToolIndex::from_raw(tool)) == filament) {
            return;
        }
        config_store().calibrated_pressure_advance.set(0.f);
        log_info(PressureAdvanceCalibration, "filament changed, calibrated value discarded");
    }

} // namespace

std::optional<float> saved_value() {
    const float value = config_store().calibrated_pressure_advance.get();
    const auto tool = VirtualToolIndex::currently_selected_opt();
    if (!(value > 0) || !tool || tool->to_raw() != config_store().calibrated_pressure_advance_tool.get()) {
        return std::nullopt;
    }
    if (FilamentType::for_tool(*tool) != FilamentType(config_store().calibrated_pressure_advance_filament.get())) {
        return std::nullopt;
    }
    return value;
}

void save(float pressure_advance) {
    const auto tool = VirtualToolIndex::currently_selected_opt();
    if (!tool) {
        return;
    }
    config_store().calibrated_pressure_advance_tool.set(tool->to_raw());
    config_store().calibrated_pressure_advance_filament.set(FilamentType::for_tool(*tool));
    config_store().calibrated_pressure_advance.set(pressure_advance);
    gcode_overrides = false;
    apply(current_context());
}

void gcode_request(std::optional<float> pressure_advance, std::optional<float> smooth_time) {
    if (pressure_advance) {
        requested.pressure_advance = *pressure_advance;
    }
    if (smooth_time) {
        requested.smooth_time = *smooth_time;
    }
    gcode_overrides = true;

    const Context context = current_context();
    if (pressure_advance && context.saved && !context.override_allowed) {
        std::array<char, 96> buffer;
        snprintf(buffer.data(), buffer.size(), "Pressure advance %.3f ignored, using the calibrated %.3f", static_cast<double>(*pressure_advance), static_cast<double>(*context.saved));
        SERIAL_ECHO_START();
        SERIAL_ECHOLN(buffer.data());
    }
    apply(context);
}

void step() {
    if (applied && ticks_diff(ticks_ms(), last_check_ms) < static_cast<int32_t>(check_interval_ms)) {
        return;
    }
    last_check_ms = ticks_ms();

    // Only change the settings with no moves in flight (no waiting from within idle) and not during a calibration
    if (pressure_advance::PressureAdvanceDisabler::is_active() || planner.processing()) {
        return;
    }

    discard_if_filament_changed();
    const Context context = current_context();
    if (applied == context) {
        return;
    }
    // A new filament or changed settings: the calibrated value takes precedence again
    gcode_overrides = false;
    apply(context);
}

} // namespace pressure_advance_calibration
