// Host test stub of freertos::Queue: no scheduler, the time does not pass.
// try_receive() returns at once. If the queue is empty and the call would wait, it first runs
// on_wait (the test uses it for a reply that arrives during the wait). A send() to a full queue
// does not block: it is counted in blocked_sends, because in the firmware it would block the
// calling task for ever.
#pragma once

#include <cstddef>
#include <deque>
#include <functional>

namespace freertos {

inline size_t blocked_sends = 0;
inline std::function<void()> on_wait;

template <class T, size_t N>
class Queue {
    std::deque<T> items;

public:
    void send(const T &payload) {
        if (items.size() >= N) {
            ++blocked_sends;
            return;
        }
        items.push_back(payload);
    }
    void receive(T &payload) {
        payload = items.front();
        items.pop_front();
    }
    [[nodiscard]] bool try_send(const T &payload, size_t) {
        if (items.size() >= N) {
            return false;
        }
        items.push_back(payload);
        return true;
    }
    [[nodiscard]] bool try_receive(T &payload, size_t milliseconds_to_wait) {
        if (items.empty() && milliseconds_to_wait > 0 && on_wait) {
            on_wait();
        }
        if (items.empty()) {
            return false;
        }
        receive(payload);
        return true;
    }
    void clear() { items.clear(); }
};

} // namespace freertos
