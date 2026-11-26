# Differential test of the motion core against Klipper

`precise_stepping`, `input_shaper` and `pressure_advance` are a port of Klipper's step generation (`itersolve.c`, `kin_shaper.c`, `kin_extruder.c`). This test sends the same motion to both and compares the step times of each stepper.

## How it works

1. `buddy_driver.cpp` puts planner blocks into a stub planner queue and runs the real `PreciseStepping::move_isr()`. Then it takes the step events from the step event queue with the real `PreciseStepping::process_one_step_event_from_queue()`, as the step ISR does. The output is the time (whole µs, as on the printer) and the direction of each step of A, B, Z and E. It also records each move segment that `PreciseStepping` makes.
2. `klipper_runner.cpp` sends these move segments, unchanged, to Klipper (`klipper/`, see its README). The kinematics are CoreXY, the input shaper pulses are the Buddy shaper definitions, and the extruder moves are queued as `klippy/kinematics/extruder.py` does.
3. `compare.cpp` pairs the steps by the step boundary that they cross and reports the time differences.

The real motion sources are compiled from a copy in the build directory, next to the stubs in `stubs/` (see `CMakeLists.txt`). The `debug_assert()` checks of the motion core stay on.

## Limits (`compare_axis()` and `check_axis()` in `compare.cpp`)

The Klipper side is the raw itersolve output, which is exact to about 1 ns. The test also asks Klipper for its exact stepper position (`kl_session_position()`: the function whose crossings itersolve finds, with the input shaper and the PA smoothing). An axis fails in four cases:

- At a Buddy step, the exact position is more than 0.25 step from the boundary that the step crosses. The test reads it at the step time and 1 µs later, because Buddy rounds the step times down to whole µs. With all firmware fixes, all cases, the fuzz inputs and the precision cases stay within 0.02 step. The exception is E after 33 m without a halt or G92 E0 (3.9 steps, the float drift of the E position), which fails as it must. `harness_mutation_tests.cpp` checks that a bias of 0.4 step, an extra step pair and a lost step fail.
- At a Klipper crossing, the Buddy position is more than 0.75 step from it. This finds lost and wrong steps.
- The end positions differ by more than 0.5 step. An end exactly on a boundary (the planner works in quarter steps) agrees with both neighbours, so a tie needs no exemption.
- The step events do not add up to the stepper position.

Two Buddy approximations are allowed in addition:

- Pressure advance (`PaErrorBound`): Buddy samples the PA position every h = `smooth_time / 40`, filters the samples and interpolates linearly. Each jump J of the PA position adds J / 40. Each jump Δv of the PA speed adds `smooth_time` × Δv / 4800. Both apply within `smooth_time` / 2 + h of the jump, also at a halt (the start of the ending empty move). `scripts/pa_bounds.py` checks the two terms. A constant acceleration a adds h² × a / 8 everywhere.
- Input shaper pulse merge (`IsPulseMergeBound`, CoreXY A and B): Buddy moves a pulse that is up to 10 µs after the last kept pulse (of either logical axis) to the time of that pulse. Klipper does not. A moved pulse with amplitude a and shift d moves its axis by up to a × d × |v| while it reads a moving segment. The terms of the moved pulses add up. `regression_is_pulse_merge.cpp` tests it.
- Input shaper (`IsEpsilonBound`): Buddy sets a micro segment acceleration with |half_accel| ≤ 0.1 mm/s² to zero and keeps the average speed. If the speed then changes sign, it sets the speed to 0. Thus the error is up to |half_accel| × T². It also sets speeds below 1e-4 mm/s to zero: up to 1e-4 × T. T is the segment length if the stepper acceleration of the segment is below the epsilon. Else T is the pulse span S, because only a micro segment across a segment boundary can have a small acceleration. `regression_is_epsilon_bound.cpp` tests both cases.

The pairing of the steps by boundary gives only statistics (time differences, steps without a partner).

After a halt, PreciseStepping starts again at time 0 and E 0. For Klipper, the driver continues the time at the end of the last real segment plus the shaper or PA tail. It continues E at the commanded E, rounded to a step for each motion. Thus lost E steps before a halt stay visible after it.

## Inputs that are compared

`scenario.cpp` makes only inputs inside these ranges. Inputs outside them are not compared with Klipper:

- Input shaper: `frequency_safe_min` to `frequency_safe_max` (10 to 100 Hz), damping 0 to 0.3, vibration reduction 10 to 30.
- Pressure advance 0.005 to 0.2, smooth time 0.01 to 0.12 s. Below 0.01 s, see `anya/pa-smooth-time-min`. Above 0.12 s, the `check_step_time()` debug check of v8.0.2 fails.
- Speeds and accelerations that the planner can make: both ends of a block are reachable within it, and a block starts at the end speed of the block before. The junctions are not limited by the geometry.
- The driver checks that Buddy takes the configuration unchanged. The only change is the rounding of the PA sample period to whole µs, and Klipper gets the same rounded smooth time.

## What is covered

- `append_move_segments_to_queue()`: trapezoid to move segments.
- The beginning and ending empty moves.
- The step event queue: merge, split of long intervals, discard of move segments and blocks.
- The classic step generator on A, B, Z and E, CoreXY.
- The input shaper step generator for X, Y (CoreXY, with the pulse merge of `input_shaper_config.cpp`) and Z. All shaper types.
- The pressure advance step generator, with retractions and travel moves.

## Defects found (v8.0.2)

The cases with `[!mayfail]` fail because of these defects. Each fix has a `regression_<topic>.cpp` test on its own branch.

1. Pressure advance steps of the last `smooth_time / 2` before a halt are lost. The PA steps lag the PA samples by `smooth_time / 2`. When the sampling reaches the ending empty move, the PA generator moves to it. The other generators wait on the ending move with an INVALID event at its start. This event is the nearest event, so `process_one_move_segment_from_queue()` ends the motion.
2. The last pressure advance step before a direction change has the new direction. When `step_generator_next_step_event()` generates a step, it copies the direction flags. When the step is emitted, the generator has already generated the next step. The classic and input shaper generators always put a keep-alive event between two steps in opposite directions, the PA generator does not.
3. A short extruder move (one sample) just before a halt makes no steps. `is_pressure_advance_reached_end()` takes two equal filter outputs as the end, but the newest sample has zero weight in the Bartlett window.
4. CoreXY: when X or Y is shaped, `update_maximum_lookback_time()` ignores the Z shaper. The beginning empty move is then too short for the Z shaper, and all step times move later by the difference.

## What is not covered

- The planner (junction speeds, block lengths): the test makes the blocks itself.
- Phase stepping, which replaces the step events on X/Y of the CORE One. The step generators are the same, but the output path is different.
- The step ISR timing (`step_isr()`, resync with phase stepping) and the move ISR scheduling.
- Changes of input shaper or pressure advance parameters during motion, and `quick_stop()`.
- Cartesian printers (the stubs set `COREXY`).
