#pragma once
#include "print_status_message_mgr.hpp"
struct PrintStatusMessageGuard {
    template <PrintStatusMessage M>
    void update(FakePrintStatusMessageArgs) {}
};
