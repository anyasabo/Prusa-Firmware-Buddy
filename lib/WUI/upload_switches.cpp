// EXPERIMENT (branch upload-switches, not for upstream). See upload_switches.hpp.
#include "upload_switches.hpp"

#include <buddy/upload_switches.h>

namespace {
constexpr upload_switches::Values boot_values {};
}

extern "C" {

volatile uint8_t upload_switch_rx_desc = boot_values.rx_desc;
volatile uint8_t upload_switch_ooseq_fix = boot_values.ooseq_fix;
volatile uint8_t upload_switch_queue_cap = boot_values.queue_cap;

void upload_switches_apply_from_path(const char *path) {
    upload_switches::Values values {
        .rx_desc = upload_switch_rx_desc,
        .ooseq_fix = upload_switch_ooseq_fix,
        .queue_cap = upload_switch_queue_cap,
    };
    if (!upload_switches::parse(path, values)) {
        return;
    }
    upload_switch_rx_desc = values.rx_desc;
    upload_switch_ooseq_fix = values.ooseq_fix;
    upload_switch_queue_cap = values.queue_cap;
    // ethernetif.c reports the switches once a second (metric "upx").
}

} // extern "C"
