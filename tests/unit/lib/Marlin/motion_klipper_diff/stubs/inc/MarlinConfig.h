#pragma once
// Stand-in for the printer configuration and the HAL of the step and move timers.
#include "MarlinConfigPre.h"
#include "../core/types.h"
#include <cmath_ext.h>

#include <algorithm>
#include <limits>

#define BOARD_IS_DWARF() 0
#define HAS_PLANNER()    1

#define BLOCK_BUFFER_SIZE       32
#define MOVE_SEGMENT_QUEUE_SIZE (2 * BLOCK_BUFFER_SIZE)
#define STEP_EVENT_QUEUE_SIZE   1024

#define STEPPER_TIMER_RATE         1000000
#define STEPPER_TIMER_TICKS_PER_US 1
#define MOVE_TIMER_FREQUENCY       1000
#define STEP_TIMER_NUM             0
#define MOVE_TIMER_NUM             1

#define INVERT_X_DIR  false
#define INVERT_Y_DIR  false
#define INVERT_Z_DIR  false
#define INVERT_E0_DIR false

// Pins: the test reads the step event queue, not the pins.
#define X_APPLY_DIR(v) ((void)(v))
#define Y_APPLY_DIR(v) ((void)(v))
#define Z_APPLY_DIR(v) ((void)(v))
#define E_APPLY_DIR(v) ((void)(v))
#define X_STEP_SET()   ((void)0)
#define Y_STEP_SET()   ((void)0)
#define Z_STEP_SET()   ((void)0)
#define E_STEP_SET()   ((void)0)
#define X_STEP_RESET() ((void)0)
#define Y_STEP_RESET() ((void)0)
#define Z_STEP_RESET() ((void)0)
#define E_STEP_RESET() ((void)0)

// Timers: a fake register set. No interrupt runs; the test calls move_isr() itself.
struct StubTimerRegs {
    uint32_t compare = 0;
    uint32_t counter = 0;
};
struct StubTimerHandle {
    StubTimerRegs handle;
};
inline StubTimerHandle TimerHandle[2];

#define TIM_CHANNEL_1       0
#define TIM_FLAG_CC1        1
#define TIM_IT_CC1          1
#define TIM8_BRK_TIM12_IRQn 0

#define __HAL_TIM_GET_COMPARE(h, ch)    ((h)->compare)
#define __HAL_TIM_SET_COMPARE(h, ch, v) ((h)->compare = (v))
#define __HAL_TIM_GET_COUNTER(h)        ((h)->counter)
#define __HAL_TIM_SET_COUNTER(h, v)     ((h)->counter = (v))
#define __HAL_TIM_GET_FLAG(h, f)        (0)
#define __HAL_TIM_CLEAR_IT(h, f)        ((void)0)
inline void NVIC_ClearPendingIRQ(int) {}
#define RESET 0

inline void HAL_timer_start(uint8_t, uint32_t) {}
inline void HAL_timer_enable_interrupt(uint8_t) {}
inline void HAL_timer_disable_interrupt(uint8_t) {}
inline void HAL_timer_isr_prologue(uint8_t) {}
inline void HAL_timer_isr_epilogue(uint8_t) {}
#define HAL_MOVE_TIMER_ISR() void stub_move_timer_isr()
#define HAL_STEP_TIMER_ISR() void stub_step_timer_isr()
