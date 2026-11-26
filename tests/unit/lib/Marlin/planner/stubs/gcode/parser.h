#pragma once
// Stand-in for the G-code parser: M200 (volumetric extrusion) is off.
struct GCodeParser {
    static inline bool volumetric_enabled = false;
};
inline GCodeParser parser;
