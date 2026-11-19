// Host test stub of freertos::Mutex: one task, no scheduler.
#pragma once
namespace freertos {
class Mutex {
public:
    void lock() {}
    void unlock() {}
    bool try_lock() { return true; }
};
} // namespace freertos
