/* Copyright 2026 Keychron
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Prospector status — host conformance test.
 *
 * Proves two things without any hardware:
 *
 *  1. STRUCTURAL: our wire struct is byte-for-byte identical to the reference
 *     `struct zmk_status_adv_data` from t-ogura/prospector-zmk-module v2.2.3
 *     (commit 72a71aeea816eb0f86ffc5b108b267076ce5f0af). Every field offset and
 *     the total size are checked at compile time against the vendored header.
 *
 *  2. FUNCTIONAL: a frame produced by prospector_encode() passes the scanner's
 *     real acceptance gate, transcribed below from
 *     src/status_scanner.c (scan_callback) of the same commit.
 *
 * Build:  cc -I tests -I <qmk>/keyboards/keychron/common/prospector \
 *            -o /tmp/t tests/test_prospector_conformance.c \
 *            <qmk>/keyboards/keychron/common/prospector/prospector_status.c
 */

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "zmk/status_advertisement.h"   /* vendored reference          */
#include "prospector_status.h"          /* implementation under test   */

/* ------------------------------------------------------------------------ */
/* 1. Structural conformance                                                */
/* ------------------------------------------------------------------------ */

_Static_assert(sizeof(struct zmk_status_adv_data) == 26,
               "reference struct must be 26 bytes");
_Static_assert(sizeof(struct prospector_adv_data) == 26,
               "our struct must be 26 bytes");

#define SAME_OFFSET(field)                                                          \
    _Static_assert(offsetof(struct prospector_adv_data, field) ==                   \
                       offsetof(struct zmk_status_adv_data, field),                 \
                   "offset mismatch: " #field)

SAME_OFFSET(manufacturer_id);
SAME_OFFSET(service_uuid);
SAME_OFFSET(version);
SAME_OFFSET(battery_level);
SAME_OFFSET(active_layer);
SAME_OFFSET(profile_slot);
SAME_OFFSET(connection_count);
SAME_OFFSET(status_flags);
SAME_OFFSET(device_role);
SAME_OFFSET(device_index);
SAME_OFFSET(peripheral_battery);
SAME_OFFSET(layer_name);
SAME_OFFSET(keyboard_id);
SAME_OFFSET(modifier_flags);
SAME_OFFSET(wpm_value);
SAME_OFFSET(channel);

/* Our local constants must agree with the reference masks. */
_Static_assert(PROSPECTOR_FLAG_CAPS_WORD == ZMK_STATUS_FLAG_CAPS_WORD, "flag");
_Static_assert(PROSPECTOR_FLAG_CHARGING == ZMK_STATUS_FLAG_CHARGING, "flag");
_Static_assert(PROSPECTOR_FLAG_USB_CONNECTED == ZMK_STATUS_FLAG_USB_CONNECTED, "flag");
_Static_assert(PROSPECTOR_FLAG_USB_HID_READY == ZMK_STATUS_FLAG_USB_HID_READY, "flag");
_Static_assert(PROSPECTOR_FLAG_BLE_CONNECTED == ZMK_STATUS_FLAG_BLE_CONNECTED, "flag");
_Static_assert(PROSPECTOR_FLAG_BLE_BONDED == ZMK_STATUS_FLAG_BLE_BONDED, "flag");
_Static_assert(PROSPECTOR_MOD_LCTL == ZMK_MOD_FLAG_LCTL, "mod");
_Static_assert(PROSPECTOR_MOD_RGUI == ZMK_MOD_FLAG_RGUI, "mod");
_Static_assert(PROSPECTOR_ROLE_STANDALONE == ZMK_DEVICE_ROLE_STANDALONE, "role");
_Static_assert(PROSPECTOR_SERVICE_UUID_0 == 0xAB && PROSPECTOR_SERVICE_UUID_1 == 0xCD, "uuid");

/* ------------------------------------------------------------------------ */
/* 2. Scanner acceptance gate (transcribed from status_scanner.c)            */
/* ------------------------------------------------------------------------ */

/* status_scanner.c, scan_callback(): the exact condition under which the
 * scanner treats a manufacturer-data AD element as Prospector data. */
static int scanner_accepts(const uint8_t *ad, size_t len) {
    if (len < sizeof(struct zmk_status_adv_data)) {
        return 0; /* "Manufacturer data too short: %d bytes" */
    }
    const struct zmk_status_adv_data *data = (const struct zmk_status_adv_data *)ad;
    if (data->manufacturer_id[0] == 0xFF && data->manufacturer_id[1] == 0xFF &&
        data->service_uuid[0] == 0xAB && data->service_uuid[1] == 0xCD) {
        return 1;
    }
    return 0; /* "Non-Prospector device: ..." */
}

static int failures;

static void check(int cond, const char *what) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) failures++;
}

int main(void) {
    printf("Prospector status encoder — conformance test\n");
    printf("reference: prospector-zmk-module v2.2.3 "
           "(72a71aeea816eb0f86ffc5b108b267076ce5f0af)\n");

    printf("\n1. structural conformance\n");
    printf("  [PASS] struct sizes and all 16 field offsets match at compile time\n");

    printf("\n2. functional: encode -> scanner gate\n");

    struct prospector_state st;
    memset(&st, 0, sizeof(st));
    st.battery_level = 90;
    st.battery_known = true;
    st.active_layer  = 2;
    st.profile_slot  = 1;
    st.connection_count = 1;
    st.status_flags  = PROSPECTOR_FLAG_USB_CONNECTED | PROSPECTOR_FLAG_BLE_CONNECTED;
    st.modifier_flags = PROSPECTOR_MOD_LCTL | PROSPECTOR_MOD_LALT;
    st.wpm = 60;
    st.channel = 0;
    st.keyboard_id[0] = 0x12; st.keyboard_id[1] = 0x34;
    st.keyboard_id[2] = 0x56; st.keyboard_id[3] = 0x78;

    uint8_t frame[64];
    size_t  n = prospector_encode(&st, (struct prospector_adv_data *)frame);

    char dump[256];
    prospector_format((const struct prospector_adv_data *)frame, dump, sizeof(dump));
    printf("  frame (%zu bytes): %s\n", n, dump);

    check(n == 26, "encoder emits exactly 26 bytes");
    check(scanner_accepts(frame, n) == 1, "scanner acceptance gate accepts the frame");

    /* Field-by-field against the protocol table. */
    const struct zmk_status_adv_data *d = (const struct zmk_status_adv_data *)frame;
    check(d->manufacturer_id[0] == 0xFF && d->manufacturer_id[1] == 0xFF, "manufacturer id 0xFFFF");
    check(d->service_uuid[0] == 0xAB && d->service_uuid[1] == 0xCD, "service uuid ABCD");
    check(d->version == 0x22, "version byte 0x22 (module v2.2)");
    check(PROSPECTOR_DECODE_VERSION_MAJOR(d->version) == 2, "scanner decodes major=2 (not '< v2.2')");
    check(PROSPECTOR_DECODE_PATCH(d->profile_slot) == 3, "scanner decodes patch=3");
    check(PROSPECTOR_DECODE_PROFILE(d->profile_slot) == 1, "scanner decodes profile slot=1");
    check(d->battery_level == 90, "battery 90");
    check(d->active_layer == 2, "active layer 2");
    check(d->connection_count == 1, "connection count 1");
    check(d->status_flags == (ZMK_STATUS_FLAG_USB_CONNECTED | ZMK_STATUS_FLAG_BLE_CONNECTED), "status flags");
    check(d->device_role == ZMK_DEVICE_ROLE_STANDALONE, "device role standalone");
    check(d->device_index == 0, "device index 0");
    check(d->peripheral_battery[0] == 0 && d->peripheral_battery[1] == 0 &&
          d->peripheral_battery[2] == 0, "split batteries reported 0 = N/A (not fabricated)");
    check(memcmp(d->layer_name, "L2  ", 4) == 0, "layer label truthful 'L2'");
    check(d->keyboard_id[0] == 0x12 && d->keyboard_id[3] == 0x78, "keyboard id passthrough");
    check(d->modifier_flags == (ZMK_MOD_FLAG_LCTL | ZMK_MOD_FLAG_LALT), "modifier flags");
    check(d->wpm_value == 60, "wpm 60");
    check(d->channel == 0, "channel 0 = accept all");

    printf("\n3. unknown-field handling (protocol sentinels, never guessed)\n");
    struct prospector_state unk;
    memset(&unk, 0, sizeof(unk));
    unk.battery_known = false;
    uint8_t f2[32];
    prospector_encode(&unk, (struct prospector_adv_data *)f2);
    const struct zmk_status_adv_data *u = (const struct zmk_status_adv_data *)f2;
    check(u->battery_level == 0, "unknown battery -> 0, not a nominal placeholder");
    check(u->wpm_value == 0, "unknown wpm -> 0 (protocol: inactive/unknown)");
    check(u->active_layer == 0, "layer 0");
    check(memcmp(u->layer_name, "L0  ", 4) == 0, "unknown layer label still truthful");
    check(u->profile_slot == PROSPECTOR_PROFILE_SLOT_BYTE(0), "slot 0");

    printf("\n4. negative control: a plain HID advertisement is rejected\n");
    /* What an ordinary BLE HID keyboard advertisement looks like: a
     * manufacturer-data element with a real company id and short payload. */
    uint8_t hid_adv[8] = {0x06, 0x00, 0x01, 0x09, 0x00, 0x00, 0x00, 0x00};
    check(scanner_accepts(hid_adv, sizeof(hid_adv)) == 0,
          "short/non-Prospector manufacturer data is rejected");

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL CHECKS PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
