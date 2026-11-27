#include "chamber_alarm.hpp"

namespace buddy {

ChamberAlarm::Actions ChamberAlarm::step(const Inputs &inputs) {
    Actions actions;

    if (inputs.critical_flag) {
        // After the first time, the critical flag stays set down to the recovery temperature, and
        // the user can heat again outside a print. Then a second time at the critical temperature
        // must turn off the heaters again. The warning comes again only for a new time at the
        // critical temperature, so that it does not come back at each step after the user closes it.
        const bool new_time_at_critical_temp = inputs.at_critical_temp && !was_at_critical_temp;
        if (!critical_warning_shown || inputs.printing || inputs.at_critical_temp) {
            actions.abort_print = true;
            actions.disable_heaters = true;
            actions.show_critical_warning = !critical_warning_shown || inputs.printing || new_time_at_critical_temp;
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

    was_at_critical_temp = inputs.at_critical_temp;
    return actions;
}

} // namespace buddy
