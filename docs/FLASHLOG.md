# Flash log — K10 Max, serial `345B36873234`

All timestamps are from the session in which this work was done. **No write
operation has been performed on the keyboard.** Every entry below is either a
read or a content-neutral request.

| # | Action | Command | Result |
|---|---|---|---|
| 1 | Enumerate USB | `ioreg -p IOUSB -w0 -l` | Keyboard absent as `3434:0AA0`. An ST `0483:DF11` ROM-DFU device present at path `1-1.1.2`, serial `345B36873234` |
| 2 | Enumerate DFU | `dfu-util -l` | 4 interfaces. `alt=0` = `@Internal Flash /0x08000000/04*016Kg,01*064Kg,01*128Kg` ⇒ 256 KB ⇒ STM32F401. Also `alt=1` Option Bytes, `alt=2` OTP, `alt=3` Device Feature |
| 3 | First contact status | `dfu-util -d 0483:df11 -a 0 -s 0x08000000:262144 -U …` | `DFU state(10) = dfuERROR, status(10) = Device's firmware is corrupt. It cannot return to run-time (non-DFU) operations` → cleared → `dfuIDLE`. Bootloader refuses to start the application |
| 4 | **Backup (read-only)** | `dfu-util -d 0483:df11 -a 0 -s 0x08000000:262144 -U backup-current-firmware.bin` | **Success**, 262144 bytes at 2048 B transfer size, 100 %. `evidence/backup-current-firmware.bin`, SHA-256 `ff0426833ede5dead8db3f1f2752a581fed73950dc292f55447fc8072fd14601` |
| 5 | Identify the dump | `strings`, vector-table parse | Contains `Keychron K10 Max`, `STM32F401`. `SP=0x20000400`, reset `0x0800811d`, Shannon entropy 3.03, ~105 KiB of code then `0xFF`/`0x00` padding ⇒ a real, unencrypted K10 Max application image |
| 6 | Compare against local builds | byte diff vs `STOCK` / `prospector` / `codex_threads` | No exact match (16 396 differing bytes vs the closest), consistent with an official Keychron release image. **Not** a build from this tree |
| 7 | Detach attempt (no write) | `dfu-util -d 0483:df11 -a 0 -e` | `dfu-util: can't detach` — ROM bootloader will not leave DFU for the current image. Keyboard still at `0483:DF11`; still no `3434:0AA0` |

## Interpretation

The keyboard was handed over **already sitting in the ROM bootloader**, not
running its application. This is why it does not appear as `3434:0AA0` on USB and
does not appear in the HID device list.

Two readings are consistent with the evidence and are not yet distinguished:

1. The board is in hardware bootloader entry (Esc/reset held during USB attach),
   in which case it stays in DFU until it is power-cycled normally — and there is
   nothing wrong with the firmware at all.
2. A previous DFU download was interrupted, leaving the ROM's error state set.

A plain power-cycle separates the two and is the next step. Only if that fails is
a write needed; the chosen recovery image is the stock Keychron build from the
pinned tree (`build/keychron_k10_max_ansi_rgb_STOCK.bin`,
SHA-256 `00e8d9f676846996bc1d644c5d3afd7a9211b59eae1a807b3e3f27682f2f7e32`).

## Files written by this work

Writes were limited to the host: the QMK tree, the deliverable directory, and
this backup. **The keyboard's flash and the LKBT51 module were not modified.**
