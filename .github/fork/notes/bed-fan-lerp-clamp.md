Test build for one fix: **bed fan PWM above the threshold** (branch `bed-fan-lerp-clamp` on upstream 9.0.0). Nothing else changed.

In the automatic mode (`M106 P5`), a temperature difference above the threshold must give the maximum PWM. The value wrapped around in an 8-bit cast, so the fan ran slow, for example at 10 %, when it must run at full speed.

## Install

This is unofficial firmware for the **Prusa CORE One L only**. Do not flash it on another printer.

1. The printer accepts unofficial firmware only after the appendix of the xBuddy board is broken.
   Read Prusa's article "Flashing custom firmware" first. This ends the warranty of the board.
2. Copy `COREONEL_<label>.bbf` to the USB drive of the printer. Select it on the printer, or flash it
   with Connect. The printer shows a warning about unofficial firmware. Accept it.
3. To go back, flash the official firmware from prusa3d.com the same way.

`SHA256SUMS-<label>.txt` has the checksums. The `.elf` file is for crash dumps and debuggers.
Problems: open an issue on this fork, not on Prusa's repository.
