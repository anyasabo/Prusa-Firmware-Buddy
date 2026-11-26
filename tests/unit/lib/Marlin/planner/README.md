# Host tests of the real planner

`planner_tests` compiles the real `lib/Marlin/Marlin/src/module/planner.cpp` as a CORE One L (1.5GT belts, junction deviation 0.01 mm, `JD_SMALL_SEGMENT_HANDLING`). The test sends move sequences to `Planner::buffer_line()`. When PreciseStepping takes a block, the test checks it.

## How it works

- `CMakeLists.txt` copies the real sources and the stubs into one tree in the build directory, as `motion_klipper_diff` does. Thus the relative includes of the real sources find the stubs.
- The configuration is real: `include/marlin/Configuration_COREONEL.h`, `Configuration_COREONEL_adv.h`, `Conditionals_LCD.h` and `Conditionals_adv.h`. `stubs/inc/MarlinConfigPre.h` defines the few values of `Conditionals_post.h` that the planner reads, because that file needs the STM32 HAL and the pins.
- The real PreciseStepping, input shaper and pressure advance sources are linked, because `planner.cpp` calls them. Their stubs come from `../motion_klipper_diff/stubs`.
- The test driver `planner_harness.cpp` plays the motion code (G0/G1, G92) and PreciseStepping: after each command it takes a random number of blocks, as the move ISR does. When the queue is full, the planner calls `idle()`, and the harness takes one block. A block that PreciseStepping took is busy: the planner cannot change it any more.
- `planner_checks.cpp` computes the limits again in double, from the mini-steps of each block and the planner configuration.

## Stubs

The stubs in `stubs/` replace the modules around the planner. Each file has a comment. They are:

- configuration: `inc/`, `hw_configuration.hpp`, `config_store/store_c_api.h`, `Marlin/src/core/macros.h`, and the option headers (`option/*.h`, written by `CMakeLists.txt`),
- the single tool of the CORE One L: `tool_index.hpp`,
- the modules that the planner calls: `Marlin.h`, `module/motion.h` (no MBL), `module/stepper.h`, `module/temperature.h` (always hot), `module/configuration_store.h`, `gcode/`, `feature/` and `freertos/`.

These options are off, because they change the position or block the queue and need their own fakes: crash recovery, emergency stop, auto retract, filament tracker, cancel object.

## Properties

For each block (`check()` in `planner_checks.cpp`):

- The entry and exit speeds are not above the nominal speed, and the nominal speed is not above the feed rate.
- The entry speed is not above the junction deviation limit. The test computes it again in double over X, Y, Z and E, with M201 and M204 F.
- The entry speed is the exit speed of the move block before.
- The block is feasible: |v_exit² − v_entry²| ≤ 2·a·L.
- M203: |vx| + |vy| (the CoreXY motor speed) ≤ X, and the Z and E speeds.
- M204: an E-only move uses R. If E moves, an XYZ move uses P. Else it uses T. M201 for each axis along the block. M204 F on print moves.
- The sum of the signed mini-steps of all blocks is the planner position. This is also true across G92 (sync blocks or a direct set) and at negative positions.

The junction limit uses the float positions of the planner (`position_float`), as the planner does. The rounded mini-steps change the direction of a short block, mostly in E.

The hand-written test cases are squares, retractions with Z hop and G92 E0, reversals, zero-length moves, and arcs of 0.02 to 2 mm segments. 40 random sequences have lengths from 0 to 100 mm.

## Defects found (v8.0.2)

The tests with `[!shouldfail][known_bug]` fail because of these defects:

1. M221 flow: `_populate_block()` scales the E of each block and rounds it to a mini-step. The rounding error does not carry to the next block. 1000 moves of 0.01 mm E at M221 S95 make 14200 mini-steps instead of 14440: 1.7 % less E than M221 asks for. Only with flow other than 100 % (or M200).
2. `JD_SMALL_SEGMENT_HANDLING`: the acos() approximation has an error of up to 0.033 rad and does not go below 0.033 rad. On an arc of radius 20 mm, at 1250 mm/s², 0.1 mm segments run at 61 mm/s and 0.3 mm segments at 105 mm/s. The centripetal limit is 158 mm/s. Fixed: the planner uses acosf().
3. A move shorter than `MIN_STEPS_PER_SEGMENT` (6 steps) goes into the next move. A G92 directly after it discards it (up to 5 steps, 0.013 mm of E).
4. Junction of a straight line where only the E per mm changes a little (Arachne width steps): the difference of the two unit vectors is float noise. Normalized, it points along E, and the M204 F limit (120 mm/s²) gives a junction limit of about 130 mm/s at 0.2 mm segments instead of the feed rate. Fixed: below a squared length of 2e-6 (as upstream Marlin #28433), the junction uses the block acceleration.

Design limits, not test failures:

- The queue has 32 blocks, and the newest block ends at `MINIMUM_PLANNER_SPEED`. Thus a full queue of short segments must be able to stop within 31 segments. At 1250 mm/s², 0.1 mm segments run at 88 mm/s at most. Klipper limits its lookahead by time, not by a number of moves.
- Counted in `Report::exit_below_minimum_speed`: the planner plans the newest block to end at `MINIMUM_PLANNER_SPEED` (0.05 mm/s). A junction limit can be lower (an E-heavy junction with a tiny XYZ part). If the block before is busy, it cannot slow down to it, and the speed jumps by less than 0.05 mm/s along the path.

## Run

`utils/build_tests.py --build-dir build-tests planner_tests`, then `ctest -R planner`.
