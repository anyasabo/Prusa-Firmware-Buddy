#pragma once
// Stand-in: no G-code is cancelled, so the planner never drains.
struct GcodeExceptions {
    bool is_unwinding() const { return false; }
    void throw_unhandled() {}
    void finish_unwinding_unhandled_exception() {}
    void report_xyz_move() {}
};
inline GcodeExceptions &gcode_exceptions() {
    static GcodeExceptions e;
    return e;
}
