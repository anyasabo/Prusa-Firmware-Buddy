Test build for one fix: **print fan RPM at low speeds** (branch `fanctl-pwm-on-int16` on upstream 9.0.0). Nothing else changed.

Below 20 % fan speed (M106 S50), the firmware measured the print fan RPM 2 to 5 times too low. A single low reading pauses the print with a print fan error. This occurs, for example, with PC filament (15 % minimum fan speed).

Please report if you had false print fan errors before, and if they stop with this build.

## Install

This is unofficial firmware for the **Prusa CORE One L only**. Do not flash it on another printer.

1. The printer accepts unofficial firmware only after the appendix of the xBuddy board is broken.
   Read Prusa's article "Flashing custom firmware" first. This ends the warranty of the board.
2. Copy `COREONEL_<label>.bbf` to the USB drive of the printer. Select it on the printer, or flash it
   with Connect. The printer shows a warning about unofficial firmware. Accept it.
3. To go back, flash the official firmware from prusa3d.com the same way.

`SHA256SUMS-<label>.txt` has the checksums. The `.elf` file is for crash dumps and debuggers.
Problems: open an issue on this fork, not on Prusa's repository.
