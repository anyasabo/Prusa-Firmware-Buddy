// Host test stub: one task, no RTOS.
#pragma once
#include <stdint.h>
typedef struct {
    int dummy;
} RWMutex_t;
static inline void rw_mutex_init(RWMutex_t *m, uint8_t n) {
    (void)m;
    (void)n;
}
static inline void rw_mutex_writer_take(RWMutex_t *m) { (void)m; }
static inline void rw_mutex_writer_give(RWMutex_t *m) { (void)m; }
