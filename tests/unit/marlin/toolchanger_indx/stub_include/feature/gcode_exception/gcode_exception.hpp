#pragma once
struct FakeGcodeExceptions {
    bool unwinding = false;
    bool is_unwinding() const { return unwinding; }
};
FakeGcodeExceptions &gcode_exceptions();
