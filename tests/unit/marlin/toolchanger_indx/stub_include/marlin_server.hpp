#pragma once
#include <cstdint>
#include <optional>
#include <fsm/nozzle_mismatch_phases.hpp>

namespace marlin_server {
bool is_printing();
void print_abort();

class FSM_Holder {
public:
    explicit FSM_Holder(PhaseNozzleMismatch phase);
    ~FSM_Holder();
    FSM_Holder(const FSM_Holder &) = delete;
};
void fsm_change(PhaseNozzleMismatch phase);
Response wait_for_response(PhaseNozzleMismatch phase);

struct FakeResponseVariant {
    std::optional<uint8_t> dock;
    template <typename T>
    const T *value_maybe() const { return dock ? &*dock : nullptr; }
};
FakeResponseVariant wait_for_response_variant(PhaseNozzleMismatch phase);
} // namespace marlin_server
