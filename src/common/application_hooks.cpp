
/// @file
/// Home for the FreeRTOS application hooks, each dispatching to a few
/// lightweight subsystem functions. vApplicationTickHook runs in the SysTick
/// ISR context, so keep everything it reaches short and non-blocking.

#include "cpu_utils.hpp"
#include <device/board.h>

#include <option/rtt_metrics_enabled.h>
#if RTT_METRICS_ENABLED()
    #include <rtt_metrics_task/rtt_metrics_task.hpp>
#endif

extern "C" void vApplicationTickHook() {
    cpu_utils::compute_cpu_load();
#if !BOARD_IS_DWARF() && !BOARD_IS_MODULARBED() // the boards that send the metric (appmain.cpp)
    cpu_utils::sample_inherited_priority();
#endif
#if RTT_METRICS_ENABLED()
    rtt_metrics::capture_stepper_positions();
#endif
}

extern "C" void vApplicationIdleHook() {
    cpu_utils::mark_cpu_idle();
}
