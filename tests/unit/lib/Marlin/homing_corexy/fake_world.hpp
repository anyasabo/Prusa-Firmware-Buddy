#pragma once
// The fake world of the CoreXY homing tests: the two XY motors, the frame walls at the home corner,
// the stepper counters and the endstops. The fakes of the planner, the steppers, the endstops and
// motion.h (stubs/module/) move this world.
//
// Coordinates:
// - rotor_a, rotor_b: the physical position of the motors in full Marlin steps. The MSCNT (the
//   electrical phase) of each motor follows its rotor position.
// - The carriage is at x2 = rotor_a + rotor_b and y2 = rotor_a - rotor_b (two times the XY steps).
// - The walls of the home corner of the CORE One L (X max, Y min) are at x2_wall and y2_wall. The
//   carriage touches a wall when x2 >= x2_wall or y2 <= y2_wall.
// - The stepper counters are rotor + offset. The planner sets the offsets when it sets a position.
#include <module/planner.h>
#include <module/stepper.h>
#include <module/endstops.h>

#include <deque>
#include <vector>

namespace fake {

struct Move {
    int32_t a0, b0; ///< rotor position at the start
    int32_t a1, b1; ///< rotor position at the end
    bool endstops; ///< the endstops were on
    bool hit; ///< the move stopped at a wall
    float y_mm; ///< machine Y at the start
};

struct HomeCall {
    AxisEnum axis;
    float x_mm, y_mm; ///< machine position at the start of the homing move
};

struct World {
    int32_t rotor_a = 0, rotor_b = 0;
    int32_t offset_a = 0, offset_b = 0;
    int32_t x2_wall = 0, y2_wall = 0;
    int phase_a = 0, phase_b = 0; ///< MSCNT at rotor position 0

    /// Steps added to the stop position of the next stall moves of one motor (a negative value is
    /// an early trigger). Each stall move of one motor takes one value.
    std::deque<int32_t> bump_noise;
    /// Steps added to the stop position of the next homeaxis() calls. A positive value is a late
    /// stall: the rotor turns past the wall (skipped steps), and wall_crashes does not count it.
    std::deque<int32_t> home_noise;

    std::vector<Move> moves;
    std::vector<HomeCall> homes;
    std::vector<float> y_moves; ///< targets of do_blocking_move_to_y()
    int wall_crashes = 0; ///< moves with the endstops off that went into a wall

    int32_t x2() const { return rotor_a + rotor_b; }
    int32_t y2() const { return rotor_a - rotor_b; }
    bool touches_wall(int32_t a, int32_t b) const { return a + b >= x2_wall || a - b <= y2_wall; }

    /// Move the rotors to (a, b). With the endstops on, stop at a wall.
    void move_rotors(int32_t a, int32_t b, bool with_endstops);

    /// Count of the stall moves of one motor (the measurement bumps of the refinement)
    size_t single_motor_bumps() const;
};

extern World world;

/// Reset the world and all fakes. The carriage is at the machine position (x, y) mm, and the
/// machine coordinates are exact (the walls are at X_MAX_POS and Y_MIN_POS). The XY motors have
/// steps_per_mm (100 for the 2GT belt, 101.587 for the 1.5GT belt).
void reset(float x_mm, float y_mm, int phase_a = 0, int phase_b = 0, float steps_per_mm = 100.f);

/// Machine position of a rotor position, with the current stepper offsets
float machine_x(int32_t a, int32_t b);
float machine_y(int32_t a, int32_t b);

} // namespace fake
