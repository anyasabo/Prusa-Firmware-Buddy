Unofficial firmware for Prusa printers: upstream 8.0.2 with all fixes of this fork. Each fix has its own branch and an upstream pull request or a draft.

**Tested only on a CORE One L.**

The images for the other printers are built from the same code. Nobody tested them on a printer. If you flash one, keep the official firmware on a USB drive.

**Version number:** this build is `8.0.2+…`. If your printer runs an earlier build of this fork (`9.0.0+….anya.1`), the bootloader sees an older version: press the knob on the bootloader screen to flash it.

## Fixes

- **Faster uploads to PrusaLink** (PR [#5598](https://github.com/prusa3d/Prusa-Firmware-Buddy/pull/5598), `nhttp-pbuf-queue-window`): 10 Ethernet RX descriptors, out-of-order TCP data kept, a larger upload queue. On a CORE One L, uploads from a Mac went from 56–76 KB/s to approximately 570 KB/s. The MINI keeps 4 RX descriptors. Also the start and the end of each upload are off the network thread.
- **Print fan** (PR [#5633](https://github.com/prusa3d/Prusa-Firmware-Buddy/pull/5633), `fanctl-pwm-on-int16-v2`): the RPM below 20 % speed is correct, so no false print fan errors with PC and other filaments at low fan speeds.
- **Thermal protection** (`thermal-runaway-gap-pr`): a hotend that stops just under its target is protected again.
- **Input shaper calibration** (`input-shaper-float-fit-v2`): the "computing" step is 9 times faster.
- **GUI loop sleeps 10 ms at idle** (`gui-idle-sleep`): on a CORE One L, the display task uses 10 % of the CPU, not 28 %, and idle uploads are faster.
- **Probe diagnostics** (`anya/probe-diagnostics`): each failed mesh point writes a `Probe` warning with the reason, and the metrics `probe_fail` and `probe_tare`. Fixes for a NaN probe result and for the touchdown dwell.
- **Security fixes:**
  - USB drives with a block size other than 512 (`usbh-msc-block-size`).
  - `.bgcode` files with very large blocks (`bgcode-skip-block-wrap`).
  - The Wi-Fi scan (`espif-scan-ap-info`).
- Smaller fixes:
  - **OpenPrintTag** temperatures that do not fit (`openprinttag-range-check`).
  - The **phase-stepping resync** in 32 bits (`phstep-resync-int32`).
  - **Bed fan PWM** (`bed-fan-lerp-clamp`).
  - **G-code parser** limits (`gcode-parser-unsigned-limits`).
  - **6 metric fixes** (`metric-fixes`).
- FatFs R0.16 and the USB host library 3.5.5, with their CVE fixes (FatFs: also CVE-2026-6686).

## Also in this build (for diagnostics)

- More metrics on by default (temperatures, fans, planner, heap, phase stepping stalls for each axis, `e_stall`). They go only to the metrics host that you set in the printer. The metric `tmc_e` (the state of the E motor driver) is off by default.
- Test G-codes `M9958`, `M9959` and `M9961`. They do nothing unless you send them.
- Runtime switches for the upload fixes. An upload whose file name contains `upx-` changes them. Do not use `upx-` in the names of your own files.

## Files

| Printer | File |
| --- | --- |
| CORE One L | `COREONEL_anya.6.bbf` |
| CORE One | `COREONE_COREONE+GEN2_anya.6.bbf` |
| CORE One with INDX | `COREONE_COREONE+GEN2_INDX_anya.6.bbf` |
| MK4, MK4S, MK3.9, MK3.9S | `MK4_MK4S_MK3.9_MK3.9S_anya.6.bbf` |
| MK3.5, MK3.5S | `MK3.5_MK3.5S_anya.6.bbf` |
| XL | `XL_XL+_anya.6.bbf` |
| MINI, MINI+ | `MINI_english-<language>_anya.6.bbf`. Select the file with your second language. |

The names are the same as the names of the official 8.0.2 files. Each `.elf` file is for crash dumps and debuggers. `SHA256SUMS-anya.6.txt` has the checksums.

## Install

This is unofficial firmware. Flash only the file for your printer.

1. The printer accepts unofficial firmware only after the appendix of its main board is broken. Read Prusa's article "Flashing custom firmware" first. This ends the warranty of the board.
2. Copy the `.bbf` file to the USB drive of the printer. Select it on the printer, or flash it with Connect. The printer shows a warning about unofficial firmware. Accept it.
3. To go back, flash the official firmware from prusa3d.com the same way.

Problems: write in the [Discussions of this fork](https://github.com/anyasabo/Prusa-Firmware-Buddy/discussions). Do not open an issue on Prusa's repository.
