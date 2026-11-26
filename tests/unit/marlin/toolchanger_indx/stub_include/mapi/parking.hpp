/// Fake of src/common/mapi/parking.hpp: same ParkingPosition shape, simplified park() in fake_world.cpp.
#pragma once
#include <core/types.h>
#include <cmath>
#include <variant>

namespace mapi {
struct ParkingPosition {
    struct Unchanged {
        constexpr bool operator==(const Unchanged &) const = default;
    };
    struct Relative {
        float delta;
        constexpr bool operator==(const Relative &) const = default;
    };
    struct AtLeast {
        [[no_unique_address]] std::monostate _use_aggregatee_initizalizer {};
        float above_print = NAN;
        float absolute = 0;
        constexpr bool operator==(const AtLeast &) const = default;
    };
    static constexpr Unchanged unchanged {};
    using X = std::variant<Unchanged, float>;
    using Y = std::variant<Unchanged, float>;
    using Z = std::variant<Unchanged, float, Relative, AtLeast>;
    X x = unchanged;
    Y y = unchanged;
    Z z = unchanged;
};
bool park(const ParkingPosition &parking_position);
} // namespace mapi
