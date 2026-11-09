Test build for one fix: **thermal protection gap under the target** (branch `thermal-runaway-gap-pr` on upstream 9.0.0). Nothing else changed.

If the hotend temperature stops just under its target (up to 6 C under it on the CORE One L), no thermal runaway protection was active. If the thermistor then fell out, the heater stayed on at full power. With this fix, the protection arms within 15 C of the target.

Normal printing must not change. Please report each thermal error that you get with this build.

## Install

This is unofficial firmware for the **Prusa CORE One L only**. Do not flash it on another printer.

1. The printer accepts unofficial firmware only after the appendix of the xBuddy board is broken.
   Read Prusa's article "Flashing custom firmware" first. This ends the warranty of the board.
2. Copy `COREONEL_<label>.bbf` to the USB drive of the printer. Select it on the printer, or flash it
   with Connect. The printer shows a warning about unofficial firmware. Accept it.
3. To go back, flash the official firmware from prusa3d.com the same way.

`SHA256SUMS-<label>.txt` has the checksums. The `.elf` file is for crash dumps and debuggers.
Problems: open an issue on this fork, not on Prusa's repository.
