#pragma once
struct FakeUI {
    void status_printf_P(int, const char *, ...) {}
};
inline FakeUI ui;
