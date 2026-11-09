// EXPERIMENT: CPU time of the GUI task by section, and of the phase stepping interrupt.
// The FreeRTOS switch hooks (FreeRTOSConfig.h) keep a CPU clock for one task: it does not count
// the time when other tasks run or when the task is blocked. Interrupt time while the task runs
// is counted (the phase stepping counter measures the largest interrupt separately).
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Called by FreeRTOS in the context switch (traceTASK_SWITCHED_IN/OUT). tcb is pxCurrentTCB.
void buddy_prof_switched_in(void *tcb);
void buddy_prof_switched_out(void *tcb);

/// Cycles of the phase stepping interrupt (wraps; the ISR adds to it).
extern volatile uint32_t buddy_phstep_isr_cycles;

/// Minimum GUI sleep in ms when nothing waits to redraw (1 = stock, up to 10). M9959 sets it.
extern volatile uint32_t buddy_gui_delay_min;

/// Phase stepping rest fast path: 0 = stock (off), 1 = on. M9959 P sets it.
extern volatile uint32_t buddy_phstep_fast;
/// Refreshes of one axis that the fast path skipped, and full refreshes (wrap; the ISR adds).
extern volatile uint32_t buddy_phstep_fast_passes;
extern volatile uint32_t buddy_phstep_full_passes;
/// Cycles of the 4 parts of a full refresh (wrap; the ISR adds). See refresh_axis().
extern volatile uint32_t buddy_phstep_part_cycles[4];

#ifdef __cplusplus
}

namespace cpu_prof {

/// The calling task becomes the measured task. Call it once from the GUI task.
void set_task_to_current();

/// CPU cycles of the measured task since set_task_to_current(). Call it only from the measured task.
uint64_t now();

enum class Section : uint8_t { tick,
    screens,
    dialog,
    gui_loop,
    draw,
    opt,
    count };

/// Adds cycles to a section. Only the measured task calls it.
void add(Section s, uint64_t cycles);

/// Takes the 32-bit phase stepping counter into 64 bits (it wraps after 25.6 s of ISR time). The
/// GUI task calls it on each redraw pass, also in modal loops that do not call loop_done().
void fold();

/// Counts one GUI loop pass, and sends the metrics about once each second.
void loop_done();

/// RAII: adds the CPU cycles of the scope to a section.
class Scope {
public:
    explicit Scope(Section s)
        : section(s)
        , start(now()) {}
    ~Scope() { add(section, now() - start); }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

private:
    Section section;
    uint64_t start;
};

} // namespace cpu_prof
#endif
