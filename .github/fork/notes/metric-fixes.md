Test build for **6 metric fixes** (branch `metric-fixes` on upstream 9.0.0). Nothing else changed.

The fixes: the rate limit after a gap of more than 35.8 minutes, `M331`/`M332`/`M334` with no argument, costly arguments on each loop pass, the reset of `plan_min`, a counter of dropped datagrams, and the free stack in bytes. Useful only if you send metrics to a host (Settings > Network > Metrics & Log).

## Install

This is unofficial firmware for the **Prusa CORE One L only**. Do not flash it on another printer.

1. The printer accepts unofficial firmware only after the appendix of the xBuddy board is broken.
   Read Prusa's article "Flashing custom firmware" first. This ends the warranty of the board.
2. Copy `COREONEL_<label>.bbf` to the USB drive of the printer. Select it on the printer, or flash it
   with Connect. The printer shows a warning about unofficial firmware. Accept it.
3. To go back, flash the official firmware from prusa3d.com the same way.

`SHA256SUMS-<label>.txt` has the checksums. The `.elf` file is for crash dumps and debuggers.
Problems: open an issue on this fork, not on Prusa's repository.
