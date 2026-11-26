# Differential test of the motion core against Klipper

`precise_stepping`, `input_shaper` and `pressure_advance` are a port of Klipper's step generation (`itersolve.c`, `kin_shaper.c`, `kin_extruder.c`). This test sends the same motion to both and compares the step times of each stepper.

## How it works

1. `buddy_driver.cpp` puts planner blocks into a stub planner queue and runs the real `PreciseStepping::move_isr()`. Then it takes the step events from the step event queue with the real `PreciseStepping::process_one_step_event_from_queue()`, as the step ISR does. The output is the time (whole µs, as on the printer) and the direction of each step of A, B, Z and E. It also records each move segment that `PreciseStepping` makes.
2. `klipper_runner.cpp` sends these move segments, unchanged, to Klipper (`klipper/`, see its README). The kinematics are CoreXY, the input shaper pulses are the Buddy shaper definitions, and the extruder moves are queued as `klippy/kinematics/extruder.py` does.
3. `compare.cpp` pairs the steps by the step boundary that they cross and reports the time differences.

The real motion sources are compiled from a copy in the build directory, next to the stubs in `stubs/` (see `CMakeLists.txt`). The `debug_assert()` checks of the motion core stay on.

## Limits (`check_axis()` in `compare.cpp`)

- A step agrees with one of two limits: the time difference is at most 25 µs + 1 µs, or it is less than half of the local step interval. 25 µs is Klipper's default `max_error`: Klipper itself sends step times that differ from its ideal times by up to this much. Buddy rounds the step times down to whole µs, which adds up to 1 µs. Near zero speed, a tiny position error is a large time error, so there the step interval is the limit.
- The planner positions are quarter steps. Thus a move can end exactly on a step boundary, and the last step at zero speed can go either way. The end positions can differ by one step, and one step for each axis can have any time.
- Near zero speed, one side can make a step pair (a step and a step back) that the other side does not make. Up to 4 steps without a partner are allowed. Klipper removes such pairs before it sends the steps to the MCU (the 750 µs rollback in `stepcompress.c`). Thus those Klipper steps have no time limit.
- Pressure advance: Buddy samples the PA position every `smooth_time / 40` and filters the samples. A jump of the PA position moves to the next sample. The jump is `pressure_advance` × the jump of the E speed, at the start or the end of an extrusion. The position error is up to jump / 20 (`pa_jump_steps()`), and this is allowed in addition.

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
