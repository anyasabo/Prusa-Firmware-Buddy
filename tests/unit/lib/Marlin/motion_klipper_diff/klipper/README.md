# Klipper step generation (test oracle)

These files are copied without changes from Klipper `klippy/chelper/` at commit [`29d4ec2d6d840ee839333306003a400da186d41d`](https://github.com/Klipper3d/klipper/tree/29d4ec2d6d840ee839333306003a400da186d41d/klippy/chelper) (2026-11-10). Klipper is GPLv3, as this firmware.

- `itersolve.c`, `itersolve.h`: the step time solver.
- `trapq.c`, `trapq.h`: the queue of moves.
- `kin_corexy.c`, `kin_cartesian.c`: the kinematics.
- `kin_shaper.c`: the input shaper.
- `kin_extruder.c`: pressure advance and smoothing.
- `compiler.h`, `list.h`, `pyhelper.h`, `stepcompress.h`: headers that these files include.

`stepcompress_recorder.c` is not from Klipper. It replaces Klipper `stepcompress.c`: it records each step time in `double` seconds and does not compress. It also keeps the rollback filter of Klipper `stepcompress_append()`: it removes a step if a step in the other direction follows within 750 µs. Thus the test can see the steps that Klipper sends to the MCU.

To update, copy the files again from a new Klipper commit and change the commit in this file.
