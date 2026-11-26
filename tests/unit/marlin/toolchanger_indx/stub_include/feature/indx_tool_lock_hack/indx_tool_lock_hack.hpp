#pragma once
#include <utils/badge.hpp>
class PrusaToolChanger;
namespace buddy {
struct IndxToolLockHack {
    int rearms = 0;
    void rearm(Badge<PrusaToolChanger>) { ++rearms; }
};
IndxToolLockHack &indx_tool_lock_hack();
} // namespace buddy
