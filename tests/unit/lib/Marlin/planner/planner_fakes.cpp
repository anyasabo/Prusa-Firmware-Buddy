// The functions that the stubs declare and the planner calls.
#include "planner_harness.hpp"

#include <Marlin.h>

void idle(bool, bool) {
    planner_harness::on_idle();
}
