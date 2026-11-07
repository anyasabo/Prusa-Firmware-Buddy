#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

extern uint32_t heap_total_size();
extern uint32_t heap_bytes_remaining();
/// Free bytes inside malloc's pool (newlib mallinfo().fordblks).
extern uint32_t heap_malloc_free();

uint32_t mem_is_heap_allocated(const void *ptr);
// Returns NULL on failure instead of redscreen
void *malloc_fallible(size_t size);

// Malloc, but returns null on failure, not a redscreen.
void *malloc_fallible(size_t size);

#ifdef __cplusplus
}
#endif //__cplusplus
