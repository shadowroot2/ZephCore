/*
 * SPDX-License-Identifier: MIT
 * BLE OTA firmware update (MCUmgr SMP over BLE) - public API
 *
 * The SMP GATT service exists only while an OTA window is open: `start ota`
 * registers it, `stop ota` or the window timeout removes it. Its
 * characteristic needs an authenticated (passkey-bonded) link. Uploaded
 * images go to MCUboot slot1; the client's reset command reboots into them.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Boot, before bt_enable(): confirm the running image, hook the MCUmgr
 * image/reset events, and take the SMP service out of the GATT table. */
void ble_ota_init(void);

/* Open the OTA window (register the SMP service). 0, -EALREADY, or -errno. */
int ble_ota_start(void);

/* Close the OTA window (unregister the SMP service). 0 or -EALREADY. */
int ble_ota_stop(void);

bool ble_ota_is_active(void);

/* Seconds left in the open window (0 when closed). */
uint32_t ble_ota_remaining_s(void);

#ifdef __cplusplus
}
#endif
