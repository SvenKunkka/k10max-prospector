# Evidence ledger

Three claims are kept strictly separate, as requested: **built**, **flashed**,
**physically verified**. A lower tier never implies a higher one.

---

## Tier 1 — BUILT ✅ (achieved)

| Claim | Evidence |
|---|---|
| Encoder matches the reference protocol byte-for-byte | `tests/test_prospector_conformance.c` — 16 `_Static_assert`s on struct size and every field offset against the vendored `zmk/status_advertisement.h` from module v2.2.3; all pass |
| Encoder output is accepted by the scanner's real gate | Same test transcribes `scan_callback()`'s acceptance condition from `status_scanner.c`; the encoded frame is accepted, a plain HID advertisement is rejected (negative control) |
| Unavailable fields are not fabricated | Test asserts `peripheral_battery[] == 0` (protocol "N/A"), unknown battery → 0, unknown WPM → 0, and truthful `L<n>` layer labels |
| Stock K10 Max firmware builds | `make keychron/k10_max/ansi/rgb:keychron` → 75 408 B, SHA-256 `00e8d9f6…` |
| Instrumented firmware builds for the real target | `make keychron/k10_max/ansi/rgb:prospector` → 77 132 B, SHA-256 `e96b3490…` (linked, ELF + bin + hex produced) |

Reproduce: `docs/BUILD.md` §3.

## Tier 2 — FLASHED ⛔ (not performed as a fix)

Nothing was flashed to the keyboard as a solution, because the built firmware
cannot perform the requested function (see `docs/BLOCKER.md`), and flashing it
would present a non-functional result as progress. The task explicitly forbids
that.

What *was* done at this tier, non-destructively:

| Claim | Evidence |
|---|---|
| The keyboard was identified live | An `0483:DF11` STM32 ROM-DFU device was present and enumerated by `dfu-util -l` as `@Internal Flash /0x08000000/04*016Kg,01*064Kg,01*128Kg` (256 KB ⇒ STM32F401) |
| It is this keyboard, not another device | Full 256 KB read-out contains the strings `Keychron K10 Max` and `STM32F401`, with a valid Cortex-M vector table |
| A byte-exact recovery image exists | `evidence/backup-current-firmware.bin`, 262 144 B, SHA-256 `ff042683…` |
| The DFU write/read path works on this hardware | `dfu-util -U` completed a full 256 KB upload at 2048 B transfer size |
| Bootloader entry procedure is known and documented | `keyboards/keychron/k10_max/readme.md`: mode switch to Cable, hold Esc (or reset button under space bar) while connecting USB |

Note the bootloader reported on first contact:

```
DFU state(10) = dfuERROR, status(10) = Device's firmware is corrupt.
It cannot return to run-time (non-DFU) operations
```

This is the ST ROM bootloader refusing to launch the application. The app image
itself is present and looks valid, so this most likely reflects how DFU was
entered. It is recorded here rather than interpreted.

## Tier 3 — PHYSICALLY VERIFIED ⛔ (blocked)

| Requirement | Status |
|---|---|
| Prospector recognises this K10 Max | **Not achievable** — the keyboard cannot emit the required advertisement (`docs/BLOCKER.md`) |
| Screen updates on keypress / layer change / connection change | **Not achievable** for the same reason |
| Normal input, combos, media keys | **Not yet tested on hardware** — the unit is currently sitting in bootloader mode, so it is not running any firmware |
| Bluetooth / 2.4 GHz / wired | **Not yet tested on hardware** |
| Remapping + lighting | **Not yet tested on hardware** |

### The observation instrument does work

The Prospector's USB CDC console was captured live
(`evidence/prospector-console-baseline.log`). It logs every advertisement it
sees, which is exactly the instrument needed to settle the question
empirically. In a 10-second capture:

```
1690 lines
0   "Prospector data found"          <- no Prospector-protocol device present
1252 "Manufacturer data too short"
249  "Non-Prospector device: 0600 0109"  etc.
  9 advertisements from 'Keychron M6'      <- a real Keychron BLE device
```

The `Keychron M6` lines matter as a **positive control**: the scanner demonstrably
sees Keychron BLE hardware, and that advertisement carries only a name — no
Prospector manufacturer data. The K10 Max is not currently advertising at all
(it is in bootloader mode), which is why it does not appear.

**The decisive experiment still to run:** with the keyboard running normal
firmware and switched to Bluetooth, the console should show its name and
`Manufacturer data too short` / `Non-Prospector device` — proving at the radio
level that the stock advertisement has no Prospector payload. See `README.md`
"Next steps".

## What would move Tier 3 to green

Physical verification of the status feature needs a radio that can carry the
frame. The recommended route (a UART-attached nRF52840 inside the case, with
`prospector_transport_send()` overridden) keeps the requested architecture —
keyboard → screen, no PC — and requires no change to the Prospector firmware and
no LKBT51 reverse engineering. Until that radio exists, Tier 3 cannot be
reached and is not claimed.
