
#include <metric.h>
#include <logging/log.hpp>
#include <FreeRTOS.h>
#include <cmsis_os.h>
#include <malloc.h>
#include <heap.h>
#include <stdint.h>
#include <array>
#include <inttypes.h>
#include <cyphal_task.hpp>

#include "puppy_metrics.hpp"

LOG_COMPONENT_REF(Metrics);

namespace metrics {

void record_puppy_system() {
    METRIC_DEF(stack, "stack", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED); // Thread stack usage
    METRIC_DEF(runtime, "runtime", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED); // Thread runtime usage
    constexpr const int32_t STACK_RUNTIME_RECORD_INTERVAL_MS = 3000; // Sample stack and runtime this often
    constexpr const size_t TASK_ARRAY_SIZE = 10;

    static uint32_t last_stack_ms = 0;
    if (ticks_diff(ticks_ms(), last_stack_ms) > STACK_RUNTIME_RECORD_INTERVAL_MS) {
        last_stack_ms = ticks_ms();
        static TaskStatus_t task_statuses[TASK_ARRAY_SIZE] = {};

#if configGENERATE_RUN_TIME_STATS
        // #error dead code found by automatic analyses (see BFW-5461)
        // Runtime since last record
        static uint32_t last_totaltime = 0;
        uint32_t totaltime = ticks_ms();
        uint32_t delta_totaltime = totaltime - last_totaltime;
        last_totaltime = totaltime;
        // t / 100 for percentage calculations
        // Compensate t * 1000 * TIM_BASE_CLK_MHZ to get from ms to portGET_RUN_TIME_COUNTER_VALUE() that uses TICK_TIMER
        delta_totaltime = 10UL * TIM_BASE_CLK_MHZ * delta_totaltime;

        // Last runtime of all threads to get delta later
        uint32_t last_runtime[TASK_ARRAY_SIZE] = {};
        for (size_t idx = 0; idx < std::size(task_statuses); idx++) {
            if ((task_statuses[idx].xTaskNumber > 0) && (task_statuses[idx].xTaskNumber <= std::size(last_runtime))) {
                last_runtime[task_statuses[idx].xTaskNumber - 1] = task_statuses[idx].ulRunTimeCounter;
            }
        }
#endif /*configGENERATE_RUN_TIME_STATS*/

        // Get stack and runtime stats
        int count = uxTaskGetSystemState(task_statuses, std::size(task_statuses), NULL);
        if (count == 0) {
            log_error(Metrics, "Failed to record stack & runtime metrics. The task_statuses array might be too small.");
        } else {
            for (int idx = 0; idx < count; idx++) {
                const char *task_name = task_statuses[idx].pcTaskName;

                // Report stack usage
                const char *stack_base = (char *)task_statuses[idx].pxStackBase;
                size_t s = 0;
                /* We can only report free stack space for heap-allocated stack frames. */
                if (mem_is_heap_allocated(stack_base)) {
                    s = malloc_usable_size((void *)stack_base);
                }
                metric_record_custom(&stack, ",n=%.7s t=%i,m=%hu", task_name, s, task_statuses[idx].usStackHighWaterMark);

#if configGENERATE_RUN_TIME_STATS
                // #error dead code found by automatic analyses (see BFW-5461)
                // Report runtime usage, runtime can overflow and the difference still be valid
                if (task_statuses[idx].xTaskNumber <= std::size(last_runtime)) {
                    const uint32_t runtime_percent = (task_statuses[idx].ulRunTimeCounter - last_runtime[task_statuses[idx].xTaskNumber - 1]) / delta_totaltime;
                    metric_record_custom(&runtime, ",n=%.7s u=%" PRIu32, task_name, runtime_percent);
                } else {
                    log_error(Metrics, "Failed to record runtime metric. The last_runtime array might be too small.");
                }
#endif /*configGENERATE_RUN_TIME_STATS*/
            }
        }
    }

    METRIC_DEF(heap, "heap", METRIC_VALUE_CUSTOM, 503, METRIC_ENABLED);
    metric_record_custom(&heap, " free=%zui,total=%zui", xPortGetFreeHeapSize(), static_cast<size_t>(heap_total_size()));

    METRIC_DEF(can_error_log, "can_error_log", METRIC_VALUE_INTEGER, 505, METRIC_ENABLED);
    metric_record_integer(&can_error_log, can::cyphal::cyphal_task.get_error_log());

    /// @note What is included in regular status messages doesn't need to be recorded here.
}

} // namespace metrics
