#pragma once
namespace buddy {
struct SafetyTimer {
    void reset_restore_blocking() {}
};
inline SafetyTimer &safety_timer() {
    static SafetyTimer t;
    return t;
}
} // namespace buddy
