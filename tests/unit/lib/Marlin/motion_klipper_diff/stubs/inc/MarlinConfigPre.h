#pragma once
// Stand-in for the printer configuration: CORE One L motion settings only.
#include <stdint.h>
#include "../core/macros.h"

#define COREXY
#define ADVANCED_STEP_GENERATORS 1

#ifndef FORCE_INLINE
    #define FORCE_INLINE __attribute__((always_inline)) inline
#endif
