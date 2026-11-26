#pragma once
// Stand-in for phase stepping. The test runs the classic, input shaper and pressure advance step
// generators only; phase stepping is off.
#include "../precise_stepping/common.hpp"

namespace phase_stepping {
namespace opts {
    inline constexpr std::size_t SUPPORTED_AXIS_COUNT = 2;
}
struct AxisState {};
inline AxisState axis_states[opts::SUPPORTED_AXIS_COUNT];

inline bool is_enabled(AxisEnum) { return false; }
inline bool any_axis_enabled() { return false; }
inline bool processing() { return false; }
inline bool motion_pending() { return false; }
inline void clear_targets() {}
inline void jump_to_position(AxisEnum, float, bool) {}
inline void init_step_generator_classic(const move_t &, classic_step_generator_t &, step_generator_state_t &) {}
inline void init_step_generator_input_shaping(const move_t &, input_shaper_step_generator_t &, step_generator_state_t &) {}
} // namespace phase_stepping
