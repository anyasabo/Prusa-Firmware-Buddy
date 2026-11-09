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
volatile uint32_t buddy_phstep_fast = 0;
volatile uint32_t buddy_phstep_fast_passes = 0;
volatile uint32_t buddy_phstep_full_passes = 0;
volatile uint32_t buddy_phstep_part_cycles[4] = {};
}

namespace {
void *volatile prof_tcb = nullptr;
volatile uint32_t prof_in_at = 0; // DWT at the last switch-in of the measured task
volatile uint64_t prof_cycles = 0; // its cycles until its last switch-out (64 bits: a long modal loop does not wrap it)

std::array<uint64_t, static_cast<size_t>(cpu_prof::Section::count)> section_cycles {};
uint64_t loops = 0;
uint64_t phstep_cycles = 0; // cumulative cycles of the phase stepping interrupt
uint32_t last_phstep = 0;
uint32_t last_report_ms = 0;

// 64-bit sums of the 32-bit phase stepping counters: [0] fast passes, [1] full passes, [2..5] parts.
std::array<uint64_t, 6> phstep_sums {};
std::array<uint32_t, 6> phstep_last {};
uint32_t phstep_read(size_t i) {
    switch (i) {
    case 0:
        return buddy_phstep_fast_passes;
    case 1:
        return buddy_phstep_full_passes;
    default:
        return buddy_phstep_part_cycles[i - 2];
    }
}

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
    last_phstep = buddy_phstep_isr_cycles;
    last_report_ms = ticks_ms();
}

uint64_t now() {
    // A context switch between the reads changes prof_cycles: then read again. A torn read of the
    // 64-bit value also differs from the second read.
    uint64_t cycles;
    uint32_t in_at, dwt;
    do {
        cycles = prof_cycles;
        in_at = prof_in_at;
        dwt = DWT->CYCCNT;
    } while (cycles != prof_cycles);
    return cycles + (dwt - in_at);
}

void add(Section s, uint64_t cycles) {
    section_cycles[static_cast<size_t>(s)] += cycles;
}

void fold() {
    const uint32_t p = buddy_phstep_isr_cycles;
    phstep_cycles += p - last_phstep;
    last_phstep = p;
    for (size_t i = 0; i < phstep_sums.size(); ++i) {
        const uint32_t v = phstep_read(i);
        phstep_sums[i] += v - phstep_last[i];
        phstep_last[i] = v;
    }
}

void loop_done() {
    ++loops;
    const uint32_t now_ms = ticks_ms();
    if (now_ms - last_report_ms < 1000) {
        return;
    }
    last_report_ms = now_ms;
    fold();
    const uint64_t task_cycles = now();

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
    // Phase stepping refresh: e fast path on (M9959 P), f and u thousands of skipped and full
    // refreshes, a..d ms of CPU time of the 4 parts of the full refreshes (cumulative, use rate()).
    METRIC_DEF(metric_phstep_prof, "phstep_prof", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
    metric_record_custom(&metric_phstep_prof, " e=%ui,f=%ui,u=%ui,a=%ui,b=%ui,c=%ui,d=%ui", static_cast<unsigned>(buddy_phstep_fast),
        static_cast<unsigned>(phstep_sums[0] / 1000), static_cast<unsigned>(phstep_sums[1] / 1000),
        static_cast<unsigned>(to_ms(phstep_sums[2])), static_cast<unsigned>(to_ms(phstep_sums[3])), static_cast<unsigned>(to_ms(phstep_sums[4])), static_cast<unsigned>(to_ms(phstep_sums[5])));
}

} // namespace cpu_prof
