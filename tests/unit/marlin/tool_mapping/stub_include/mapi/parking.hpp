#pragma once
#include <cmath>
#include <variant>
#include <core/types.h>
namespace mapi {
enum class ParkPosition { park,
    unload };
struct ParkingPosition {
    struct Unchanged {};
    struct AtLeast {
        [[no_unique_address]] std::monostate _use_aggregatee_initizalizer {};
        float above_print = NAN;
        float absolute = 0;
    };
    std::variant<Unchanged, float> x = Unchanged {};
    std::variant<Unchanged, float> y = Unchanged {};
    std::variant<Unchanged, float, AtLeast> z = Unchanged {};
};
ParkingPosition get_parking_position(ParkPosition);
bool park(const ParkingPosition &);
} // namespace mapi
