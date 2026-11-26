#pragma once
#include <tool_index.hpp>
struct WaitForHotendArgs {
    bool no_wait_for_cooling = true;
    bool fan_cooling = false;
};
struct FakeThermalManager {
    int target[8] {};
    int degTargetHotend(PhysicalToolIndex t) const { return target[t.to_raw()]; }
    void setTargetHotend(int temp, PhysicalToolIndex t) { target[t.to_raw()] = temp; }
    void wait_for_hotend(PhysicalToolIndex, WaitForHotendArgs) {}
};
extern FakeThermalManager thermalManager;
