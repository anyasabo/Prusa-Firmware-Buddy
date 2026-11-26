#pragma once
// Stand-in: Motion_Parameters::reset() calls it. The tests do not use it.
struct MarlinSettings {
    static void reset_motion(bool) {}
};
