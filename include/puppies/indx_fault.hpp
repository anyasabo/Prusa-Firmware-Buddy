/// @file
/// What the main board does when the INDX head reports a fault bit.
#pragma once

#include <cstdint>
#include <optional>

#include <error_codes.hpp>
#include <indx_head/errors.hpp>

namespace buddy::puppies::indx_fault {

/// The redscreen to show for a fault of the INDX head.
struct Redscreen {
    /// Error code of the redscreen. ErrCode::ERR_UNDEF shows `text` instead.
    ErrCode code;

    /// Message for ErrCode::ERR_UNDEF, else nullptr.
    const char *text;

    bool operator==(const Redscreen &) const = default;
};

/// Title of the redscreen that shows `Redscreen::text`.
inline constexpr const char *redscreen_title = "INDX HEAD ERROR";

/// Return the redscreen for one fault bit, or std::nullopt if the main board only logs it.
std::optional<Redscreen> redscreen_for(indx_head::errors::FaultStatusMask bit);

/// Return the redscreen for the fault status register, or std::nullopt if the main board only logs it.
/// If more bits are set, the bit with the highest priority wins.
std::optional<Redscreen> redscreen_for_status(uint16_t fault_status);

} // namespace buddy::puppies::indx_fault
