#pragma once
// Stand-in for the config store: only the input shaper configuration, which the test sets.
#include <feature/input_shaper/input_shaper_config.hpp>

struct StubConfigStore {
    input_shaper::Config input_shaper_config {};
    input_shaper::Config get_input_shaper_config() const { return input_shaper_config; }
    void set_input_shaper_config(const input_shaper::Config &config) { input_shaper_config = config; }
};

inline StubConfigStore &config_store() {
    static StubConfigStore store;
    return store;
}
