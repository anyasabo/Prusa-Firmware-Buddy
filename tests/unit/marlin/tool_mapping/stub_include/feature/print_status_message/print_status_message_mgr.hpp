#pragma once
enum class PrintStatusMessage { joining_spool,
    spool_joined };
struct FakePrintStatusMessageArgs {};
struct FakePrintStatusMessageMgr {
    template <PrintStatusMessage M>
    void show_temporary(FakePrintStatusMessageArgs) {}
};
FakePrintStatusMessageMgr &print_status_message();
