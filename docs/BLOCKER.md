# Blocker: the K10 Max cannot emit the Prospector advertisement

**Status: hard blocker, not yet resolved. No firmware flashed as a fix.**

This document states exactly where the implementation stops, what evidence
proves it, and what would be needed to unblock it.

---

## 1. The one-line answer

Modifying the QMK firmware of the K10 Max is **not sufficient**. The BLE
advertisement is generated inside the **LKBT51 wireless module** — a separate
SoC on its own SPI bus — and the module's command interface exposes **no way to
set an advertisement payload**. The STM32F401 that runs QMK is not a radio and
contains no BLE stack at all.

---

## 2. What the Prospector requires (verified)

The scanner accepts status only as a 26-byte **manufacturer-specific
advertisement** with company id `0xFFFF` followed by `AB CD`:

```c
/* prospector-zmk-module v2.2.3, src/status_scanner.c, scan_callback() */
if (ad_type == BT_DATA_MANUFACTURER_DATA) {
    if (len >= sizeof(struct zmk_status_adv_data)) {          /* >= 26 bytes */
        const struct zmk_status_adv_data *data = ...;
        if (data->manufacturer_id[0] == 0xFF && data->manufacturer_id[1] == 0xFF &&
            data->service_uuid[0] == 0xAB && data->service_uuid[1] == 0xCD) {
            /* accepted */
        }
    }
}
```

and it observes with `bt_le_scan_start(&scan_param, scan_callback)` using
`BT_LE_SCAN_TYPE_ACTIVE`. There is **no GATT/connection-based ingest path** for
status — the scanner is a passive observer of advertisements.

So the keyboard must inject a 26-byte manufacturer-data AD element into its own
BLE advertising. Everything below is about whether the K10 Max can do that.

---

## 3. Where the advertisement is actually produced on the K10 Max

The K10 Max is a **split-brain** design:

| Part | Role |
|---|---|
| **STM32F401** (256 KB flash, QMK/ChibiOS) | matrix scan, keymap, layers, RGB, USB HID, battery gauge, mode switch |
| **LKBT51 module** (separate SoC, on SPI1) | the entire Bluetooth/2.4 GHz stack, HID-over-BLE, **GAP advertising**, bonding, profiles |

The evidence:

- `keyboards/keychron/k10_max/ansi/rgb/keyboard.json`:
  `"processor": "STM32F401"`, `"bootloader": "stm32-dfu"`, `"usb": {"pid": "0x0AA0"}`
- `keyboards/keychron/k10_max/config.h` pins the module:
  `LKBT51_RESET_PIN C4`, `LKBT51_INT_INPUT_PIN B1`, `BLUETOOTH_INT_OUTPUT_PIN A4`
- `keyboards/keychron/common/wireless/lkbt51.h`: `#define WT_DRIVER SPID1` — the
  module is spoken to over SPI.
- `keyboards/keychron/common/wireless/wireless.mk` compiles `lkbt51.c` for this
  board (the `ckbt51` driver is the newer module used by e.g. `k8_pro`).

**The STM32 never touches BLE.** A search of the whole Keychron QMK tree for any
advertising API returns nothing:

```
$ grep -rniE 'advertis|bt_le_adv|scan_rsp|manufacturer_data|adv_data' keyboards/keychron/
(no matches)
```

QMK's Bluetooth support on these boards is a **serial bridge**: QMK formats an
HID report, hands it to `lkbt51_send_keyboard()`, and the module transmits it.
Advertising is entirely the module's business.

---

## 4. The module's command interface has no advertising control

The complete command set, from `keyboards/keychron/common/wireless/lkbt51.c`:

| Range | Commands |
|---|---|
| `0x11`–`0x17` | HID reports (keyboard, NKRO, consumer, system, FN, mouse, boot KB) |
| `0x21`–`0x25` | `PAIRING`, `CONNECT`, `DISCONNECT`, `SWITCH_HOST`, `READ_STATE_REG` |
| `0x31`–`0x33` | `BATTERY_MANAGE`, `UPDATE_BAT_LVL`, `UPDATE_BAT_STATE` |
| `0x40`–`0x4A` | `GET_MODULE_INFO`, `SET/GET_CONFIG`, `SET/GET_BDA`, `SET/GET_NAME`, `WRTE_CSTM_DATA`, `SET_MS_SWIFT_PAIR_NAME` |
| `0x60`–`0x65` | DFU: `GET_DFU_VER`, **`HAND_SHAKE_TOKEN`**, `START_DFU`, `SEND_FW_DATA`, `VERIFY_CRC32`, `SWITCH_FW` |
| `0x71`–`0x73` | `FACTORY_RESET`, `IO_TEST`, `RADIO_TEST` |
| `0x91`–`0x93` | `RAW_HID_INIT`, `RAW_HID_RX`, `RAW_HID_TX` |

There is **no command that sets advertisement data, scan-response data, or
manufacturer-specific data.** The only GAP-visible thing QMK can influence is
the device **name** (`SET_NAME 0x45`) and the vendor/product id in the module's
config (`SET_CONFIG 0x41`). Changing the name is not the Prospector protocol,
and the scanner would still reject it — it matches on manufacturer data only.

Two candidate escape hatches, both checked and both closed:

- **`lkbt51_dfu_rx` (raw-HID `0xAA` passthrough)** could in principle smuggle
  arbitrary commands to the module from the host. It is filtered:
  ```c
  if ((payload[0] & 0xF0) == 0x60) {   /* DFU command range only */
      lkbt51_send_cmd(payload, payload_len - 2, data[1] == 0x56, retry);
  }
  ```
  Only `0x60`–`0x6F` reaches the module, and only the host DFU tool drives it.
- **`WRTE_CSTM_DATA` (`0x49`)** is dead code in the public tree — declared and
  implemented in `lkbt51.c`, but called from nowhere in `keyboards/`. Nothing
  indicates it reaches the advertising payload, and it cannot be verified
  without module documentation.

---

## 5. The module firmware itself is not modifiable with available material

| Requirement | Status |
|---|---|
| Module source code | **Not published.** Keychron's public repo contains only the STM32-side SPI driver. |
| Module SDK / chip datasheet | **Not published.** "LKBT51" is a module part number with no public SoC/SDK documentation. |
| A firmware image to patch | Vendor ships `*.kfw` only. The copy in this workspace (`keychron_spi_tmode_fw0.2.4_2511251135.kfw`, 98 684 bytes) has **Shannon entropy 7.9983 bits/byte** — indistinguishable from random, i.e. encrypted and/or compressed. No plaintext, no readable strings, no vector table. |
| Write access to the module | DFU is gated by `LKBT51_CMD_HAND_SHAKE_TOKEN` (`0x61`) before `START_DFU` (`0x62`) and `VERIFY_CRC32` (`0x64`). The token is generated and validated *inside* the module; QMK only relays opaque frames. Without vendor signing material a third-party image cannot be accepted. |

Conclusion: there is **no path** from the available source, tooling and firmware
images to an LKBT51 image that emits a Prospector advertisement.

---

## 6. Consequences for the requested deliverable

- Step 2's question is answered: **QMK-only modification is not enough, and the
  wireless module cannot be modified either with what exists.**
- The requested outcome ("keyboard talks to the Prospector directly, no PC
  relay") is **not achievable on stock K10 Max hardware** by firmware alone.
- The encoded frame is implemented and verified (see `README.md`), but it has
  no radio to leave through. **The firmware was therefore not flashed as a
  fix**, per instruction.

---

## 7. What would unblock it — in order of feasibility

### Option A — add a small BLE radio to the keyboard (recommended)

Keep the K10 Max, its module and the Prospector firmware exactly as they are.
Add one cheap BLE SoC (e.g. an nRF52840 module) inside the case:

```
STM32F401 ──UART──> nRF52840 ──BLE advertising (AD type 0x09, 26 bytes)──> Prospector
   (QMK)             (tiny Zephyr/nRF5 app)
```

- The STM32 already produces the exact 26-byte frame (`prospector_status.c`).
  Override `prospector_transport_send()` to write it to a spare UART.
- The radio firmware is ~100 lines: take 26 bytes from UART, put them in an
  advertisement as `BT_DATA_MANUFACTURER_DATA`, advertise non-connectably on the
  same intervals.
- Touches none of the keyboard's existing radios, so input / Bluetooth / 2.4 GHz
  / wired / remapping / lighting are untouched.
- No change to Prospector firmware. No PC in the loop.

What is needed: **one nRF52840 board** (a second XIAO nRF52840 is ideal, and
would let us reuse the Prospector's own proven ZMK/nRF toolchain), plus a free
UART and a power tap in the case. Soldering required, no firmware reverse
engineering.

### Option B — replace the LKBT51 module with an open module

Replace the module with a BLE SoC running firmware that speaks both HID and the
Prospector protocol. Strictly more work than A, and it puts the keyboard's
normal Bluetooth/2.4 GHz function at risk. Not recommended.

### Option C — obtain vendor material

Only if Keychron supplies, under NDA or otherwise: the LKBT51 firmware source or
a documented API, the module's SoC/SDK, and DFU signing material (or a debug
build that skips `HAND_SHAKE_TOKEN`). Without at least an "add this
manufacturer-data AD element" command, this is not tractable.

### Explicitly rejected

Using the PC to forward status (a bridge, plugin or resident service). That is
what the task rules out, and it is not implemented here.

---

## 8. Note on the 2.4 GHz path

The K10 Max also has a 2.4 GHz mode with its own USB dongle. The Prospector's
XIAO nRF52840 is BLE-only (`bluetooth_classic: false`, no proprietary 2.4 GHz
receiver), so that transport is not a viable route to the scanner either.
