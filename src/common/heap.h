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
/// EXPERIMENT (upload-experiments): number of free chunks in malloc's pool and
/// the size of the largest one (bytes, with the chunk header), from a walk of
/// newlib-nano's free list under the malloc lock.
extern void heap_malloc_chunks(uint32_t *free_chunks, uint32_t *largest_chunk);

uint32_t mem_is_heap_allocated(const void *ptr);
// Returns NULL on failure instead of redscreen
void *malloc_fallible(size_t size);

// Malloc, but returns null on failure, not a redscreen.
void *malloc_fallible(size_t size);

#ifdef __cplusplus
}
#endif //__cplusplus
