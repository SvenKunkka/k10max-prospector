# Prospector status protocol — as verified against v2.2.3

Sources (both cloned and pinned for this work):

| Component | Repo | Revision |
|---|---|---|
| Scanner (firmware on the XIAO) | `t-ogura/zmk-config-prospector` | tag `v2.2.3`, commit `391cd16a33ca0e2b62199d298273ed36cc6de2f9` |
| Shared module (protocol of record) | `t-ogura/prospector-zmk-module` | tag `v2.2.3`, commit `72a71aeea816eb0f86ffc5b108b267076ce5f0af` |

The scanner's ingest code is `src/status_scanner.c`; the keyboard-side sender is
`src/status_advertisement.c`. Both live in the **module**, so the same protocol
definition is used by both ends.

---

## 1. Transport

A **BLE legacy advertisement**, AD type `0x09` (Manufacturer Specific Data),
26 bytes, placed in ADV and/or SCAN_RSP. The scanner listens in observer mode:

```c
struct bt_le_scan_param scan_param = {
    .type    = BT_LE_SCAN_TYPE_ACTIVE,   /* sends SCAN_REQ, reads SCAN_RSP */
    .options = BT_LE_SCAN_OPT_NONE,
    .interval = BT_GAP_SCAN_FAST_WINDOW,
    .window   = BT_GAP_SCAN_FAST_WINDOW,
};
bt_le_scan_start(&scan_param, scan_callback);
```

No connection is made and no GATT service is involved for status. The keyboard's
own HID connection to the PC is independent — the module's own design notes call
this "Hybrid ADV": *piggyback* the payload onto the keyboard's existing
advertising while disconnected, and start a separate **non-connectable**
advertisement while connected, so the status stream does not consume a
connection slot.

Legacy advertising budget check: Flags (3) + manufacturer-data header (2) + 26
payload bytes = 31 bytes, exactly the legacy limit.

## 2. Payload layout (26 bytes)

| Offset | Field | Meaning |
|---|---|---|
| 0–1 | `manufacturer_id` | `FF FF` — protocol discriminator (not a SIG company id) |
| 2–3 | `service_uuid` | `AB CD` — Prospector protocol id |
| 4 | `version` | `[7:4]` major, `[3:0]` minor. v2.2.3 ⇒ `0x22`. Major `0` ⇒ scanner shows "&lt; v2.2" |
| 5 | `battery_level` | 0–100 % |
| 6 | `active_layer` | 0–15 |
| 7 | `profile_slot` | `[6]` dev flag, `[5:3]` patch, `[2:0]` profile 0–4 |
| 8 | `connection_count` | connected BLE devices, 0–5 |
| 9 | `status_flags` | see below |
| 10 | `device_role` | 0 standalone, 1 central, 2 peripheral |
| 11 | `device_index` | split index (0 = left) |
| 12–14 | `peripheral_battery[3]` | left / right / aux; **0 = N/A** |
| 15–18 | `layer_name[4]` | ASCII, not necessarily NUL-terminated |
| 19–22 | `keyboard_id[4]` | hardware-unique id (ZMK uses HWINFO) |
| 23 | `modifier_flags` | one bit per HID modifier |
| 24 | `wpm_value` | **0 = inactive/unknown** |
| 25 | `channel` | **0 = accept all** |

### `status_flags` (offset 9)

| Bit | Meaning |
|---|---|
| 0 | Caps Word active |
| 1 | Charging |
| 2 | USB connected (powered) |
| 3 | USB HID ready |
| 4 | BLE connected |
| 5 | BLE bonded |
| 6–7 | reserved |

### `modifier_flags` (offset 23)

Bit 0…7 = `LCTL, LSFT, LALT, LGUI, RCTL, RSFT, RALT, RGUI` — i.e. exactly the
HID keyboard modifier byte.

## 3. Scanner acceptance gate

```c
if (ad_type == BT_DATA_MANUFACTURER_DATA) {
    if (len >= sizeof(struct zmk_status_adv_data)) {          /* >= 26 */
        if (data->manufacturer_id[0] == 0xFF && data->manufacturer_id[1] == 0xFF &&
            data->service_uuid[0] == 0xAB && data->service_uuid[1] == 0xCD) {
            /* -> channel check -> accepted */
        }
    }
}
```

Then a channel filter: accepted if scanner channel is 0 (all), scanner channel
≥ 10, keyboard channel is 0, or the two are equal.

Anything else is logged and dropped:

```
<dbg> zmk: scan_callback: Manufacturer data too short: 22 bytes
<dbg> zmk: scan_callback: Non-Prospector device: 0600 0109
```

There is no fallback to the device name, RSSI, or a GATT read: a keyboard with a
correct name but no Prospector manufacturer data is simply invisible to the
status display.

## 4. Broadcast policy (keyboard side)

| Situation | Behaviour |
|---|---|
| Typing | 1000 ms interval (`ZMK_STATUS_ADV_INTERVAL_MS`, default 1000) |
| Idle | 30000 ms, engaged 5000 ms after the last keypress |
| Layer / modifier / profile change | immediate burst, **5 frames at 15 ms** |
| Boot | a burst, so the scanner shows the profile right away |
| Split central still bringing up peripherals | burst/silent cycle (200 ms adv / 1800 ms silent) so scan/connect is not starved |

## 5. Fields the K10 Max can and cannot supply

Verified against Keychron's QMK accessors (`wireless.h`, `battery.h`,
`transport.h`, `lpm.h`, `action_util.h`, `wpm.h`, `caps_word.h`):

| Field | K10 Max source | Notes |
|---|---|---|
| `battery_level` | `battery_get_percentage()` | real gauge; 0 + unknown when no reading |
| `active_layer` | `get_highest_layer(layer_state \| default_layer_state)` | real, live |
| `modifier_flags` | `get_mods()` | `MOD_BIT()` == HID bits, so direct |
| `wpm_value` | `get_current_wpm()` | `WPM_ENABLE` |
| `status_flags` | `usb_power_connected()`, `get_transport()`, `wireless_get_state()`, `gpio_read_pin(BAT_CHARGING_PIN)`, `is_caps_word_on()` | all real |
| `profile_slot` | `wireless_get_host_index()` (0–2; `BT_HOST_DEVICES_COUNT` = 3) | 0 when not in Bluetooth mode |
| `connection_count` | derived from `wireless_get_state() == WT_CONNECTED` | single-link keyboard; exactly 1 or 0, never guessed |
| `keyboard_id` | STM32 `UID_BASE` | real hardware id |
| `layer_name` | none available in QMK | encoder emits truthful `L<n>` |
| `device_role`, `device_index` | standalone keyboard | 0 / 0 |
| `peripheral_battery[]` | not a split keyboard | 0 = protocol "N/A", **not** fabricated |
| `channel` | configured | default 0 = accept all |
