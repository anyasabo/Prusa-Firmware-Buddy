#include <stdlib.h>

// The mbedtls allocator of the firmware (heap.cpp).
void *calloc_fallible(size_t nmemb, size_t size) {
    return calloc(nmemb, size);
}
