#include "chamber_alarm.hpp"

namespace buddy {

ChamberAlarm::Actions ChamberAlarm::step(const Inputs &inputs) {
    Actions actions;

    if (inputs.critical_flag) {
        if (!critical_warning_shown || inputs.printing) {
            actions.abort_print = true;
            actions.disable_heaters = true;
            actions.show_critical_warning = true;
            critical_warning_shown = true;
        }
    } else if (inputs.overheating_flag) {
        if (!inputs.overheating_warning_active && !overheating_warning_shown) {
            actions.show_overheating_warning = true;
            overheating_warning_shown = true;
        }
    } else {
        overheating_warning_shown = false;
        critical_warning_shown = false;
    }

    return actions;
}

} // namespace buddy
