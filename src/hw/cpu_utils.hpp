/**
 * @file
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int osGetCPUUsage();

/**
 * @brief Set a pointer to idle task watchdog reset function.
 * @note function MUST NOT, UNDER ANY CIRCUMSTANCES,
 * CALL A FUNCTION THAT MIGHT BLOCK.
 */
void osSetIdleTaskWatchdog(void (*function)());

namespace cpu_utils {

    void mark_cpu_idle();
    void compute_cpu_load();

    /// Count this tick if the task that it interrupted runs at a priority that it inherited
    /// from a mutex (priority inheritance). Call it from the tick hook only.
    void sample_inherited_priority();

    /// Count of ticks since boot in which the running task had an inherited priority.
    uint32_t inherited_priority_ticks();

} // namespace cpu_utils

#ifdef __cplusplus
}
#endif
