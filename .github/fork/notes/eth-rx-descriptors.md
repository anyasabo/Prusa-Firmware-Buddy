Test build for one fix: **10 Ethernet RX descriptors instead of 4** (branch `eth-rx-descriptors` on upstream 9.0.0). Nothing else changed.

Bursts of frames overrun the 4 descriptors and the printer drops them. Uploads to PrusaLink then wait for retransmissions. Measured on a CORE One L: an upload from macOS went from about 44 KB/s to about 230 KB/s. Cost: 9 KB of RAM.

Please report your upload speed before and after (the same file, the same computer).

## Install

This is unofficial firmware for the **Prusa CORE One L only**. Do not flash it on another printer.

1. The printer accepts unofficial firmware only after the appendix of the xBuddy board is broken.
   Read Prusa's article "Flashing custom firmware" first. This ends the warranty of the board.
2. Copy `COREONEL_<label>.bbf` to the USB drive of the printer. Select it on the printer, or flash it
   with Connect. The printer shows a warning about unofficial firmware. Accept it.
3. To go back, flash the official firmware from prusa3d.com the same way.

`SHA256SUMS-<label>.txt` has the checksums. The `.elf` file is for crash dumps and debuggers.
Problems: open an issue on this fork, not on Prusa's repository.
