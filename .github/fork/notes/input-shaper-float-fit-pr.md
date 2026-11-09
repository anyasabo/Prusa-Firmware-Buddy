Test build for one fix: **input shaper fitting in `float`** (branch `input-shaper-float-fit-pr` on upstream 9.0.0). Nothing else changed.

The fitting used `double`, which the MCU emulates in software. In `float`, the "computing" step of the input shaper calibration is 9.2 times faster, with the same result.

Please report the time of the "computing" screen, and the shaper type and frequency that it finds.

## Install

This is unofficial firmware for the **Prusa CORE One L only**. Do not flash it on another printer.

1. The printer accepts unofficial firmware only after the appendix of the xBuddy board is broken.
   Read Prusa's article "Flashing custom firmware" first. This ends the warranty of the board.
2. Copy `COREONEL_<label>.bbf` to the USB drive of the printer. Select it on the printer, or flash it
   with Connect. The printer shows a warning about unofficial firmware. Accept it.
3. To go back, flash the official firmware from prusa3d.com the same way.

`SHA256SUMS-<label>.txt` has the checksums. The `.elf` file is for crash dumps and debuggers.
Problems: open an issue on this fork, not on Prusa's repository.
