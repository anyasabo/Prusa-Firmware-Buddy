/// Header-only mutex for these tests. The freertos-mock Mutex checks with REQUIRE() in unlock(), and
/// the global SpoolJoin locks it in its constructor, before Catch2 runs.
#pragma once
namespace freertos {
class Mutex {
public:
    Mutex() = default;
    Mutex(const Mutex &) = delete;
    Mutex &operator=(const Mutex &) = delete;
    void lock() {}
    void unlock() {}
    bool try_lock() { return true; }
};
} // namespace freertos
