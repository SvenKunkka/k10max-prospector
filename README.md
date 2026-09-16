# K10 Max → Prospector: direct status, without a PC

Work product for getting a **Keychron K10 Max** to drive an assembled
**Prospector** status screen (Seeed XIAO nRF52840 Sense + Beekeeb adapter +
Waveshare 1.69" touch, firmware `prospector_scanner_touch-xiao_ble_nrf52840_zmk`,
v2.2.3) with no forwarding program, plugin or resident service on the computer.

---

## Bottom line

**The requested outcome is not reachable on stock K10 Max hardware by firmware
alone, and the blocker is not in the QMK firmware — it is the closed wireless
module.** Details and proof: **[`docs/BLOCKER.md`](docs/BLOCKER.md)**.

The K10 Max is a split-brain design. An STM32F401 runs QMK (keymap, layers,
lighting, USB HID, battery); a separate **LKBT51** SoC on SPI owns the entire
Bluetooth stack **including GAP advertising**. The Prospector does not accept
status over a connection — it passively observes BLE advertisements and matches
a 26-byte manufacturer-specific payload. QMK has no BLE stack at all
(`grep -rniE 'advertis|bt_le_adv|manufacturer_data' keyboards/keychron/` → no
matches), and the LKBT51 command set has **no command to set advertising data**.
The module's own firmware ships only as an encrypted blob and its DFU is gated by
a vendor handshake token.

So: steps 1–3 of the task are answered and implemented as far as physics allows;
step 4 (build) is done but **nothing was flashed as a fix**; step 5's status
verification is blocked, and the keyboard-side normal-function tests were not run
because the unit is currently parked in bootloader mode.

## What is actually delivered

| | |
|---|---|
| [`docs/BLOCKER.md`](docs/BLOCKER.md) | Exactly where it stops, with file/line evidence, and three concrete unblock paths |
| [`docs/PROTOCOL.md`](docs/PROTOCOL.md) | The verified Prospector v2.2.3 protocol: 26-byte layout, acceptance gate, broadcast policy, and which fields the K10 Max can really supply |
| [`docs/VERIFICATION.md`](docs/VERIFICATION.md) | Evidence ledger separating **built** / **flashed** / **physically verified** |
| [`docs/BUILD.md`](docs/BUILD.md) | Exact build target, toolchain, build/flash commands, bootloader entry, and recovery |
| [`docs/FLASHLOG.md`](docs/FLASHLOG.md) | Every DFU interaction with the board, and its result |
| `patches/k10max-prospector.patch` | 693-line patch, 9 files, +629 lines — the complete source change |
| `build/` | Built firmware + `SHA256SUMS.txt` |
| `evidence/` | The byte-exact firmware backup read off the keyboard, plus live Prospector console captures |
| `tests/` | Host conformance test against the real ZMK reference struct |
| `tools/watch-prospector.sh` | One-command radio-level verification: captures the scanner console and reports whether a named keyboard's advertisement carries the Prospector payload |

## Current hardware state (important)

The keyboard was handed over **already sitting in the STM32 ROM bootloader**, not
running its application — it does not enumerate as `3434:0AA0` and is absent from
the HID device list. Its application image is present with a valid vector table,
but the bootloader reports *"firmware is corrupt — cannot return to run-time"* and
refuses a DFU detach (`docs/FLASHLOG.md`).

A byte-exact 256 KB backup was read out first, so the previous state is
recoverable in one command. No write has been performed. A plain power-cycle is
the next step; only if that fails is a firmware write needed, with the stock
Keychron build from the pinned tree as the agreed image.

### The keyboard-side implementation

A real status encoder and scheduler, written against the actual protocol rather
than approximated:

- `keyboards/keychron/common/prospector/prospector_status.{h,c}` — pure,
  hardware-free encoder producing the exact 26-byte frame. Verified at compile
  time against the reference `struct zmk_status_adv_data` (all 16 field offsets
  and the total size), so it cannot silently drift from the protocol.
- `keyboards/keychron/common/prospector/prospector_qmk.{h,c}` — collects live
  state (layer, modifiers, WPM, battery, charging, transport, Bluetooth profile
  slot, USB state, caps-word, STM32 UID) and applies the reference broadcast
  policy: 1 Hz while typing, 30 s idle, 5×15 ms burst on layer/modifier/profile
  change, burst on boot.
- `keyboards/keychron/k10_max/ansi/rgb/keymaps/prospector/` — the stock Keychron
  keymap verbatim plus the module. All four layers, multimedia keys, Bluetooth
  slot keys, 2.4 GHz, RGB controls and VIA remapping are unchanged.
- One 3-line additive accessor in `wireless.c/.h` to expose the active Bluetooth
  host slot; no behaviour change.

Unavailable fields follow the protocol's own "unknown" sentinels instead of
invented values: split batteries report `0` (= N/A), unknown battery and WPM
report `0`, and layer labels are the truthful `L<n>` because QMK has no layer-name
table on this board. The conformance test asserts each of these.

```console
$ /tmp/prospector_test
  [PASS] encoder emits exactly 26 bytes
  [PASS] scanner acceptance gate accepts the frame
  ...
ALL CHECKS PASSED (0 failures)
```

## Verified against upstream, not assumed

| Component | Pinned revision |
|---|---|
| Scanner | `t-ogura/zmk-config-prospector` @ `v2.2.3` = `391cd16a…` |
| Protocol of record | `t-ogura/prospector-zmk-module` @ `v2.2.3` = `72a71aee…` |
| QMK | `Keychron/qmk_firmware` branch `2025q3` @ `eb4afcb` |

The linked-blog fact from earlier research is confirmed and superseded:
`BT_MODE_CLASSIC` in the pairing parameters is a red herring — `BRorLE` is
documented in `lkbt51.h` as "only available for dual mode module; keep 0 for
single mode module". The real constraint is not the pairing mode but the absence
of any advertising-data command, which no pairing parameter can work around.

## Next steps — pick one

**To unblock properly (recommended):** add one nRF52840 board inside the case,
wired to a spare STM32 UART. The STM32 already produces the finished 26-byte
frame; the radio needs ~100 lines to take those bytes and advertise them as
`BT_DATA_MANUFACTURER_DATA`. No Prospector change, no PC, no module reverse
engineering. See `docs/BLOCKER.md` §7 Option A.

**To settle the blocker empirically right now (no hardware needed):** with the
keyboard running normal firmware and switched to Bluetooth, watch the Prospector
console:

```bash
python3 /tmp/readserial.py /dev/cu.usbmodem1114404 20 | grep -i keychron
```

Expect the keyboard's name with `Manufacturer data too short` — radio-level proof
that the stock advertisement carries no Prospector payload.

**To restore the keyboard:** it is currently in STM32 bootloader mode and not
running any firmware. Recovery is one command, and a byte-exact backup of its
previous state already exists (`docs/BUILD.md` §6).

## Scope and safety notes

- Nothing was written to the keyboard during this work; the backup was a read.
- No vendor SDK, encrypted firmware or proprietary blob is redistributed here.
- The vendored reference header in `tests/zmk/` is MIT-licensed, copied verbatim
  from the pinned module revision, and used only for compile-time conformance.
- The XIAO UF2 and generic ZMK images were never a candidate for the keyboard;
  `docs/BUILD.md` §0 explains why.

## What is not in this repository

Two firmware images used by this work are **not** distributed here, because they are
Keychron's rather than this work's. Their SHA-256 are recorded so that an image you
obtain yourself — from the vendor's stock firmware, or dumped from your own board
before flashing — can be checked against the ones this work used:

| Image | SHA-256 | What it is |
|---|---|---|
| `build/keychron_k10_max_ansi_rgb_STOCK.bin` | `00e8d9f676846996bc1d644c5d3afd7a9211b59eae1a807b3e3f27682f2f7e32` | the vendor stock image this build replaced |
| `evidence/backup-current-firmware.bin` | `ff0426833ede5dead8db3f1f2752a581fed73950dc292f55447fc8072fd14601` | the dump taken from this keyboard before anything was flashed — byte-for-byte the factory image |

`build/SHA256SUMS.txt` lists only the images that are present here: the patched QMK
build this work produced, in `.bin`, `.elf` and `.hex`. The two above stay in the
working tree on the machine that did the work, so flashing can still be undone; they
are simply not published.
