#pragma once
// The fake endstops: a move with the endstops on stops at a wall of the fake world (fake_world.cpp).
#include "../inc/MarlinConfig.h"

class Endstops {
public:
    bool enabled = false;
    bool enabled_globally = false;
    uint8_t hit_state = 0;

    void enable(bool on) { enabled = on; }
    bool is_enabled() const { return enabled; }
    void not_homing() { enabled = enabled_globally; }
    uint8_t trigger_state() const { return hit_state; }
    void hit_on_purpose() { hit_state = 0; }
};
extern Endstops endstops;
