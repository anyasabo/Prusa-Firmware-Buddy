#pragma once
#include <optional>
#include <tool_index.hpp>
namespace buddy {
struct FakeFilamentTracker {
    std::optional<float> get_retracted_distance(PhysicalToolIndex) const { return std::nullopt; }
};
FakeFilamentTracker &filament_tracker();
} // namespace buddy
