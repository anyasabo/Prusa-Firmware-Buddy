#pragma once
#include <tool_index.hpp>
class Odometer_s {
public:
    static Odometer_s &instance();
    void add_toolpick(PhysicalToolIndex tool);
    int toolpicks = 0;
};
