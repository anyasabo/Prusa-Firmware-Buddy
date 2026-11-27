#pragma once

namespace buddy {

/// Decides how the printer responds to a chamber that is too hot.
///
/// The flags come from FanCooling. The caller does the actions.
class ChamberAlarm {
public:
    struct Inputs {
        /// FanCooling::get_critical_temp_flag()
        bool critical_flag;

        /// FanCooling::get_overheating_temp_flag()
        bool overheating_flag;

        /// marlin_server::is_printing()
        bool printing;

        /// marlin_server::is_warning_active(WarningType::ChamberOverheatingTemperature)
        bool overheating_warning_active;
    };

    struct Actions {
        bool abort_print = false;
        bool disable_heaters = false;
        bool show_critical_warning = false;
        bool show_overheating_warning = false;
    };

    [[nodiscard]] Actions step(const Inputs &inputs);

private:
    bool overheating_warning_shown = false;
    bool critical_warning_shown = false;
};

} // namespace buddy
