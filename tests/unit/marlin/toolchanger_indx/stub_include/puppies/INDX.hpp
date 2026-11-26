#pragma once
#include <cstdint>
#include <optional>
namespace buddy::puppies {
/// Fake INDX head puppy. The fake world in fake_world.cpp answers the nozzle presence.
struct FakeIndx {
    std::optional<bool> get_nozzle_present() const;
    void invalidate_nozzle_data();
    uint32_t get_reset_counter() const { return 0; }
    void set_hotend_target_temp(uint16_t) {}
};
extern FakeIndx indx;
} // namespace buddy::puppies
