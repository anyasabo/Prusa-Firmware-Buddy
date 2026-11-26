#pragma once
#include <cstdint>
struct G28Flags {
    bool only_if_needed = false;
    float z_raise = 0;
    bool can_calibrate = true;
    bool precise = true;
};
struct GcodeSuite {
    static bool G28_no_parser(bool X, bool Y, bool Z, const G28Flags &flags);
};
