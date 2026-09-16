# Build, flash and recovery — Keychron K10 Max (ANSI RGB)

## 0. Identify the exact variant first

`0x3434:0x0AA0` is **not** unique across the K10 Max family; it maps to one
target in the pinned tree:

```
$ grep -rn '0x0AA0' keyboards/keychron/k10_max/*/*/keyboard.json
keyboards/keychron/k10_max/ansi/rgb/keyboard.json:3:        "pid": "0x0AA0",
```

⇒ build target **`keychron/k10_max/ansi/rgb`**.

Never write another board's firmware here. In particular the XIAO UF2
(`prospector_scanner_touch-xiao_ble_nrf52840_zmk-*.uf2`) and any generic ZMK
image are for the **nRF52840 status screen** and must never be sent to the
keyboard: different ISA, different bootloader, different chip.

Board facts confirmed from the pinned source:

| | |
|---|---|
| MCU | STM32F401 (256 KB flash — confirmed live: `04*016Kg,01*064Kg,01*128Kg`) |
| Bootloader | `stm32-dfu` (ST ROM DFU, `0483:DF11`) |
| USB id (app) | `3434:0AA0` |
| Wireless | LKBT51 module on SPI1, reset `C4`, int `B1`, select `A4` |
| Mode switch | hardware: Cable / 2.4 GHz / Bluetooth (`A9`, `A10` sense pins) |
| Storage | 2048 B logical EEPROM over 4096 B embedded flash |

## 1. Toolchain

```bash
arm-none-eabi-gcc   # /opt/homebrew/bin/arm-none-eabi-gcc  (used here)
dfu-util            # /opt/homebrew/bin/dfu-util 0.11
make                # Apple make
```

QMK tree used: `/Users/keychron/qmk_firmware`, branch `2025q3`,
HEAD `eb4afcb Add Keychron K17 Max ANSI/ISO/JIS`.

## 2. Back up whatever is on the keyboard **before** writing anything

DFU read-out works on this board (RDP is not set), so a byte-exact backup is
possible. With the keyboard in bootloader mode:

```bash
dfu-util -d 0483:df11 -a 0 -s 0x08000000:262144 -U backup-current-firmware.bin
```

The backup taken for this work is `evidence/backup-current-firmware.bin`,
262144 bytes:

```
ff0426833ede5dead8db3f1f2752a581fed73950dc292f55447fc8072fd14601
```

It contains the strings `Keychron K10 Max` and `STM32F401`, a valid vector table
(`SP=0x20000400`, reset `0x0800811d`) and ~105 KiB of application code, so it is
a genuine K10 Max image. **Keep it**: it is the only copy of the state this
keyboard was in when handed over.

Also worth recording on this board: VIA remapping lives in EEPROM, which is in
the STM32's own flash, so the EEPROM region is inside the 262144-byte dump.

## 3. Build

```bash
cd /Users/keychron/qmk_firmware

# stock reference build (Keychron's own keymap, no changes)
make keychron/k10_max/ansi/rgb:keychron -j8

# Prospector-instrumented build (stock keymap + status module)
make keychron/k10_max/ansi/rgb:prospector -j8
```

Outputs land in `.build/`. Saved copies and hashes are in `build/`:

| File | SHA-256 |
|---|---|
| `build/keychron_k10_max_ansi_rgb_STOCK.bin` | `00e8d9f676846996bc1d644c5d3afd7a9211b59eae1a807b3e3f27682f2f7e32` |
| `build/keychron_k10_max_ansi_rgb_prospector.bin` | `e96b34907606c4a86e8f3fa07071caf8943f27645e70038d40da422734171d9a` |
| `build/keychron_k10_max_ansi_rgb_prospector.hex` | `e1f6b7cf815c71d7c5991d8bd09e1b487f130eca290839c092a5ca7b9daf6631` |
| `build/keychron_k10_max_ansi_rgb_prospector.elf` | `595307646b7182d43656f7627167be36f065d72e640184520a6e8b1337cf1886` |

Sizes: stock 75 408 B, instrumented 77 132 B (`text+data`) — both well inside the
256 KB part, so the extra ~1.7 KB is not a space concern.

The encoder itself is host-verifiable without any hardware:

```bash
cd k10max-prospector
cc -std=c11 -Wall -Wextra -I tests \
   -I /Users/keychron/qmk_firmware/keyboards/keychron/common/prospector \
   -o /tmp/prospector_test tests/test_prospector_conformance.c \
   /Users/keychron/qmk_firmware/keyboards/keychron/common/prospector/prospector_status.c
/tmp/prospector_test        # -> ALL CHECKS PASSED
```

## 4. Enter the bootloader (this needs your hands)

Per `keyboards/keychron/k10_max/readme.md`:

> **Reset Key**: Toggle mode switch to "Cable", hold down the *Esc* key or reset
> button underneath space bar while connecting the USB cable.

Steps:

1. Unplug USB.
2. Set the mode switch on the back to **Cable**.
3. Hold **Esc** (or press the small reset button under the space bar).
4. Plug USB in while holding, then release.
5. Confirm the bootloader appeared:

```bash
dfu-util -l
# expect: Found DFU: [0483:df11] ... name="@Internal Flash  /0x08000000/04*016Kg,01*064Kg,01*128Kg"
```

The keyboard's normal USB identity (`3434:0AA0`) disappears while in this mode,
and the keyboard is **not** usable as a keyboard until it is flashed and reset.

## 5. Flash

Either:

```bash
cd /Users/keychron/qmk_firmware
make keychron/k10_max/ansi/rgb:prospector:flash
```

or explicitly with dfu-util:

```bash
dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D build/keychron_k10_max_ansi_rgb_prospector.bin
```

Then unplug/replug USB. Success looks like re-enumeration as `3434:0AA0`.

## 6. Recovery

Three routes, in order of preference:

**A. Restore the exact previous image**

```bash
dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D evidence/backup-current-firmware.bin
```

**B. Restore the stock Keychron build from the pinned tree** (identical to
Keychron's own `keychron` keymap):

```bash
dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D build/keychron_k10_max_ansi_rgb_STOCK.bin
```

**C. Vendor firmware** — Keychron publishes K/Max-series firmware and the
Launcher web flasher. Launcher needs a Chromium browser and USB; it restores a
factory image without any of this toolchain. Useful if the STM32 ever refuses
DFU or the mode switch is suspect.

**If the keyboard looks dead after a flash:** it is overwhelmingly likely still
in bootloader mode rather than bricked. `dfu-util -l` will still list
`0483:df11` on a Cortex-M with a valid ST ROM bootloader, and route A or B
recovers it. The ST ROM bootloader itself is not erasable by a normal flash, so
this board cannot be hard-bricked by writing a bad application image.

**EEPROM/VIA settings:** a plain flash of the application region does not
rewrite the EEPROM backing area, so key remaps survive. A backup taken with
route A is byte-exact and restores both.

## 7. What is *not* recoverable

The LKBT51 module firmware is a separate device behind the STM32. The STM32-side
DFU does not touch it, and there is no dump of it in this workspace. If the
module were ever written with a bad image, recovery would need Keychron's module
DFU tool and a correct `.kfw` — see `docs/BLOCKER.md` §5. Nothing in this work
writes to the module.
