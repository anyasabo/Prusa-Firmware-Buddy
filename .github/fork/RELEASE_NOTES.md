Unofficial firmware for the Prusa CORE One L: upstream 9.0.0 with all fixes of this fork.
Each fix has its own branch and an upstream pull request or a draft.

## Fixes

- **Faster uploads to PrusaLink** (2 to 10 times, more with a bloated USB drive): 10 Ethernet RX
  descriptors (`eth-rx-descriptors`), a larger upload queue (`nhttp-pbuf-queue-window`, PR #5598),
  out-of-order TCP data kept, and the start and end of each upload moved off the network thread.
- **GUI loop sleeps 10 ms at idle** (`gui-idle-sleep`): the display task uses 10 % of the CPU, not
  28 %, and idle uploads are faster.
- **Thermal protection** (`thermal-runaway-gap-pr`): a hotend that stops just under its target is
  protected again.
- **Print fan** (`fanctl-pwm-on-int16`): the RPM below 20 % speed is correct, so no false print fan
  errors with PC and other filaments at low fan speeds.
- **Input shaper calibration** (`input-shaper-float-fit-pr`): the "computing" step is 9 times faster.
- **Bed fan PWM** (`bed-fan-lerp-clamp`), **G-code parser** (`gcode-parser-unsigned-limits`) and
  **6 metric fixes** (`metric-fixes`).
- FatFs R0.16 and the USB host library 3.5.5, with their CVE fixes.

## Also in this build (for diagnostics)

- More metrics on by default (temperatures, fans, planner, heap, phase stepping stalls for each
  axis). They go only to the metrics host that you set in the printer.
- Test G-codes `M9958` and `M9959`. They do nothing unless you send them.

## Install

This is unofficial firmware for the **Prusa CORE One L only**. Do not flash it on another printer.

1. The printer accepts unofficial firmware only after the appendix of the xBuddy board is broken.
   Read Prusa's article "Flashing custom firmware" first. This ends the warranty of the board.
2. Copy `COREONEL_<label>.bbf` to the USB drive of the printer. Select it on the printer, or flash it
   with Connect. The printer shows a warning about unofficial firmware. Accept it.
3. To go back, flash the official firmware from prusa3d.com the same way.

`SHA256SUMS-<label>.txt` has the checksums. The `.elf` file is for crash dumps and debuggers.
Problems: open an issue on this fork, not on Prusa's repository.
