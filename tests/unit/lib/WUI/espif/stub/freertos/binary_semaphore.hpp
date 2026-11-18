// Host test stub of freertos::BinarySemaphore: one task, no scheduler.
#pragma once
namespace freertos {
class BinarySemaphore {
    bool given = false;

public:
    void release() { given = true; }
    void release_from_isr() { given = true; }
    void acquire() { given = false; } // the test releases it before (the TX callback runs at once)
};
} // namespace freertos
