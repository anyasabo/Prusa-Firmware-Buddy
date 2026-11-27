#pragma once

#include <cstdint>

enum class AxisHomeLevel : uint8_t {
  /// The axis it not homed at all, we could be anywhere
  not_homed,

  /// The axis is homed imprecisely (say +- 1mm). Good enough for some operations, not good enough for printing
  imprecise,

  /// The axis is homed as precisely as the printer allows
  full
};
