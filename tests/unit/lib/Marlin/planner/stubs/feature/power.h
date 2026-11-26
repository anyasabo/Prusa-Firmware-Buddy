#pragma once
// Stand-in: AUTO_POWER_CONTROL. The planner switches the power on before a move.
struct Power {
    static void power_on() {}
};
inline Power powerManager;
