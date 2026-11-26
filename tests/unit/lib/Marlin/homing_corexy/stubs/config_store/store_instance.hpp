#pragma once
// A fake config store with the two items of the CoreXY homing.
#include <module/prusa/homing_corexy_config.hpp>

template <typename T>
struct FakeStoreItem {
    T default_value;
    T value = default_value;
    T get() const { return value; }
    void set(const T &v) { value = v; }
    void set_to_default() { value = default_value; }
};

struct FakeConfigStore {
    FakeStoreItem<CoreXYGridOrigin> corexy_grid_origin { COREXY_NO_GRID_ORIGIN };
    FakeStoreItem<CoreXYHomeTMCSens> corexy_home_tmc_sens { COREXY_NO_HOME_TMC_SENS };
};

FakeConfigStore &config_store();
