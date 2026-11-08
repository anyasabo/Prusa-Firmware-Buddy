// EXPERIMENT: see include/buddy/cpu_prof.h.
#include <buddy/cpu_prof.h>

#include "FreeRTOS.h"
#include "task.h"
#include "metric.h"
#include "stm32f4xx.h"
#include <timing.h>
#include <array>

extern "C" {
volatile uint32_t buddy_phstep_isr_cycles = 0;
volatile uint32_t buddy_gui_delay_min = 1;
}

namespace {
void *volatile prof_tcb = nullptr;
volatile uint32_t prof_in_at = 0; // DWT at the last switch-in of the measured task
volatile uint32_t prof_cycles = 0; // its cycles until its last switch-out (wraps)

std::array<uint64_t, static_cast<size_t>(cpu_prof::Section::count)> section_cycles {};
uint64_t loops = 0;
uint64_t task_cycles = 0; // cumulative CPU cycles of the measured task
uint64_t phstep_cycles = 0; // cumulative cycles of the phase stepping interrupt
uint32_t last_task_now = 0;
uint32_t last_phstep = 0;
uint32_t last_report_ms = 0;

uint32_t to_ms(uint64_t cycles) {
    return static_cast<uint32_t>(cycles / (SystemCoreClock / 1000)); // wraps after 49 days
}
} // namespace

extern "C" void buddy_prof_switched_in(void *tcb) {
    if (tcb == prof_tcb) {
        prof_in_at = DWT->CYCCNT;
    }
}

extern "C" void buddy_prof_switched_out(void *tcb) {
    if (tcb == prof_tcb) {
        prof_cycles = prof_cycles + (DWT->CYCCNT - prof_in_at);
    }
}

namespace cpu_prof {

void set_task_to_current() {
    taskENTER_CRITICAL();
    prof_in_at = DWT->CYCCNT;
    prof_cycles = 0;
    prof_tcb = xTaskGetCurrentTaskHandle();
    taskEXIT_CRITICAL();
    last_task_now = now();
    last_phstep = buddy_phstep_isr_cycles;
    last_report_ms = ticks_ms();
}

uint32_t now() {
    // A context switch between the reads changes prof_cycles: then read again.
    uint32_t cycles, in_at, dwt;
    do {
        cycles = prof_cycles;
        in_at = prof_in_at;
        dwt = DWT->CYCCNT;
    } while (cycles != prof_cycles);
    return cycles + (dwt - in_at);
}

void add(Section s, uint32_t cycles) {
    section_cycles[static_cast<size_t>(s)] += cycles;
}

void loop_done() {
    ++loops;
    const uint32_t now_ms = ticks_ms();
    if (now_ms - last_report_ms < 1000) {
        return;
    }
    last_report_ms = now_ms;
    // Take the 32-bit counters into 64 bits at least each 25 s (they wrap after 25.6 s).
    const uint32_t t = now();
    task_cycles += t - last_task_now;
    last_task_now = t;
    const uint32_t p = buddy_phstep_isr_cycles;
    phstep_cycles += p - last_phstep;
    last_phstep = p;

    using enum Section;
    const auto ms = [](Section s) { return to_ms(section_cycles[static_cast<size_t>(s)]); };
    // Cumulative counters (use rate() or increase()): n loops, all others ms of CPU time.
    METRIC_DEF(metric_gui_prof, "gui_prof", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
    metric_record_custom(&metric_gui_prof, " n=%ui,t=%ui,s=%ui", static_cast<unsigned>(loops), static_cast<unsigned>(ms(tick)), static_cast<unsigned>(ms(screens)));
    METRIC_DEF(metric_gui_prof2, "gui_prof2", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
    metric_record_custom(&metric_gui_prof2, " d=%ui,g=%ui,r=%ui", static_cast<unsigned>(ms(dialog)), static_cast<unsigned>(ms(gui_loop)), static_cast<unsigned>(ms(draw)));
    METRIC_DEF(metric_gui_prof3, "gui_prof3", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
    metric_record_custom(&metric_gui_prof3, " o=%ui,c=%ui,p=%ui", static_cast<unsigned>(ms(opt)), static_cast<unsigned>(to_ms(task_cycles)), static_cast<unsigned>(to_ms(phstep_cycles)));
    METRIC_DEF(metric_gui_min, "gui_min", METRIC_VALUE_INTEGER, 0, METRIC_ENABLED);
    metric_record_integer(&metric_gui_min, static_cast<int>(buddy_gui_delay_min));
}

} // namespace cpu_prof
