Unofficial firmware for the Prusa CORE One L: upstream 8.0.2 with all fixes of this fork.
Each fix has its own branch and an upstream pull request or a draft.

**Version number:** upstream master went back from 9.0.0 to 8.0.2 on 2026-11-16, so this build is
`8.0.2+…`. If your printer runs an earlier build of this fork (`9.0.0+….anya.1`), the bootloader
sees an older version: press the knob on the bootloader screen to flash it.

## Fixes

- **Faster uploads to PrusaLink** (PR #5598, `nhttp-pbuf-queue-window`): 10 Ethernet RX
  descriptors, out-of-order TCP data kept, a larger upload queue. From a Mac, 56–76 KB/s become
  approx. 570 KB/s. Also the start and end of each upload are off the network thread.
- **Print fan** (PR #5633, `fanctl-pwm-on-int16-v2`): the RPM below 20 % speed is correct, so no
  false print fan errors with PC and other filaments at low fan speeds.
- **Thermal protection** (`thermal-runaway-gap-pr`): a hotend that stops just under its target is
  protected again.
- **Input shaper calibration** (`input-shaper-float-fit-v2`): the "computing" step is 9 times faster.
- **GUI loop sleeps 10 ms at idle** (`gui-idle-sleep`): the display task uses 10 % of the CPU, not
  28 %, and idle uploads are faster.
- **Security fixes:** USB drives with a block size other than 512 (`usbh-msc-block-size`),
  `.bgcode` files with very large blocks (`bgcode-skip-block-wrap`), and the Wi-Fi scan
  (`espif-scan-ap-info`).
- **OpenPrintTag** temperatures that do not fit (`openprinttag-range-check`), the **phase-stepping
  resync** in 32 bits (`phstep-resync-int32`), **bed fan PWM** (`bed-fan-lerp-clamp`), **G-code
  parser** (`gcode-parser-unsigned-limits`) and **6 metric fixes** (`metric-fixes`).
- FatFs R0.16 and the USB host library 3.5.5, with their CVE fixes (FatFs: also CVE-2026-6686).

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
