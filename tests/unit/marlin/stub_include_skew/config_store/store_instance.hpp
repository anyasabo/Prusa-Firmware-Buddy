#pragma once

#include <module/planner_skew.h>

/// Stub config store with only the skew item
struct StubConfigStore {
    struct {
        skew_factor_t value { XY_SKEW_FACTOR, XZ_SKEW_FACTOR, YZ_SKEW_FACTOR };

        skew_factor_t get() const { return value; }
        void set(const skew_factor_t &v) { value = v; }
    } skew_factor;
};

inline StubConfigStore &config_store() {
    static StubConfigStore store;
    return store;
}
