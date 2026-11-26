#pragma once
namespace buddy::standard_feedrates {
enum class Extruder { retract,
    deretract };
inline float current_extruder(Extruder) { return 35; }
} // namespace buddy::standard_feedrates
