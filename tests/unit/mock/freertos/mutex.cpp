/// @file
#include <freertos/mutex.hpp>

#include <stdexcept>

thread_local int freertos::Mutex::locked_mutex_count = 0;

freertos::Mutex::Mutex() {
}

freertos::Mutex::~Mutex() {
}

void freertos::Mutex::lock() {
    locked_mutex_count++;
}

void freertos::Mutex::unlock() {
    // Throw rather than REQUIRE: fails a Catch2 test the same way, and also works outside one
    // (e.g. in the fuzz harnesses under tests/fuzz).
    if (locked_mutex_count <= 0) {
        throw std::logic_error("freertos::Mutex::unlock() without a matching lock()");
    }
    locked_mutex_count--;
}
