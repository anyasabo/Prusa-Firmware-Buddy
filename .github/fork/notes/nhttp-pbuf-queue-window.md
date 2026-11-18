Test build for pull request [#5598](https://github.com/prusa3d/Prusa-Firmware-Buddy/pull/5598) (branch `nhttp-pbuf-queue-window` on upstream 8.0.2). It has three fixes for uploads to PrusaLink. Nothing else changed.

1. 10 Ethernet RX descriptors instead of 4. Bursts of frames overran the 4 descriptors, and the printer dropped them.
2. A PrusaLink connection can keep 3 out-of-order segments, instead of none. After a lost frame, the computer then sends only the lost frame again.
3. The upload queue holds a full TCP window of 512 B segments (macOS uses this size after some losses).

Measured on a CORE One L, an upload from macOS: about 44 KB/s before, about 240 KB/s after. The numbers are in issue [#5591](https://github.com/prusa3d/Prusa-Firmware-Buddy/issues/5591).

Please report your upload speed before and after (the same file, the same computer). If your uploads fail with HTTP 408, tell if this build stops the failures.

## Install

This is unofficial firmware for the **Prusa CORE One L only**. Do not flash it on another printer.

1. The printer accepts unofficial firmware only after the appendix of the xBuddy board is broken.
   Read Prusa's article "Flashing custom firmware" first. This ends the warranty of the board.
2. Copy `COREONEL_<label>.bbf` to the USB drive of the printer. Select it on the printer, or flash it
   with Connect. The printer shows a warning about unofficial firmware. Accept it.
3. To go back, flash the official firmware from prusa3d.com the same way.

`SHA256SUMS-<label>.txt` has the checksums. The `.elf` file is for crash dumps and debuggers.
Problems: open an issue on this fork, not on Prusa's repository.
