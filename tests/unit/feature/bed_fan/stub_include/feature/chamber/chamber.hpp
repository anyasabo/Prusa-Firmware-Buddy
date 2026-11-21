/// @file
/// Stub of the chamber for the bed fan controller tests.
#pragma once

#include <optional>

namespace buddy {

class Chamber {
public:
    std::optional<float> current_temperature() const { return current; }
    std::optional<float> target_temperature() const { return target; }

    std::optional<float> current;
    std::optional<float> target;
};

Chamber &chamber();

} // namespace buddy
