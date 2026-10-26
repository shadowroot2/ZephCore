/*
 * SPDX-License-Identifier: MIT
 * BLE OTA firmware update: MCUmgr SMP over BLE into MCUboot slot1.
 *
 * The SMP service is registered at runtime (BT_GATT_DYNAMIC_DB), so it is
 * absent from the boot-time GATT table and the static handles bonded phones
 * have cached never move. `start ota` opens a window; each accepted upload
 * chunk keeps it open; the timeout or `stop ota` closes it. The SMP
 * characteristic needs an authenticated link (MCUMGR_TRANSPORT_BT_PERM_RW_AUTHEN).
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt_defines.h>
#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt.h>
#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt_callbacks.h>
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt_callbacks.h>
#include <zephyr/mgmt/mcumgr/transport/smp_bt.h>

#include "ble_ota.h"
#include "zephyr_poweroff.h"

LOG_MODULE_REGISTER(ble_ota, LOG_LEVEL_INF);

/* While an upload runs, the window is kept at least this long ahead. */
#define BLE_OTA_ACTIVITY_HOLD_S 60

static atomic_t ota_active;
static K_MUTEX_DEFINE(ota_lock);
static uint8_t last_pct_logged;

static void ota_close_work_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(ota_close_work, ota_close_work_fn);

static void ota_close_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	if (ble_ota_stop() == 0) {
		LOG_INF("BLE OTA window timed out");
	}
}

static enum mgmt_cb_return img_event_cb(uint32_t event, enum mgmt_cb_return prev_status,
					int32_t *rc, uint16_t *group, bool *abort_more,
					void *data, size_t data_size)
{
	ARG_UNUSED(prev_status);
	ARG_UNUSED(abort_more);
	ARG_UNUSED(data_size);

	switch (event) {
	case MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK: {
		/* Refuse uploads outside an open window. */
		if (!atomic_get(&ota_active)) {
			*rc = MGMT_ERR_EACCESSDENIED;
			*group = 0;
			return MGMT_CB_ERROR_RC;
		}
		const struct img_mgmt_upload_check *chk = data;

		if (k_work_delayable_remaining_get(&ota_close_work) <
		    (k_ticks_t)k_ms_to_ticks_ceil32(BLE_OTA_ACTIVITY_HOLD_S * 1000U)) {
			k_work_reschedule(&ota_close_work, K_SECONDS(BLE_OTA_ACTIVITY_HOLD_S));
		}
		if (chk->req->off == 0) {
			last_pct_logged = 0;
			LOG_INF("BLE OTA upload: %llu bytes", chk->action->size);
		} else if (chk->action->size > 0) {
			uint8_t pct = (uint8_t)((chk->req->off * 100ULL) / chk->action->size);

			if (pct >= last_pct_logged + 10) {
				last_pct_logged = pct - (pct % 10);
				LOG_INF("BLE OTA upload: %u%%", pct);
			}
		}
		break;
	}
	case MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED:
		LOG_WRN("BLE OTA upload stopped");
		break;
	case MGMT_EVT_OP_IMG_MGMT_DFU_PENDING:
		LOG_INF("BLE OTA upload complete, image in slot1");
		break;
	default:
		break;
	}
	return MGMT_CB_OK;
}

static enum mgmt_cb_return os_event_cb(uint32_t event, enum mgmt_cb_return prev_status,
				       int32_t *rc, uint16_t *group, bool *abort_more,
				       void *data, size_t data_size)
{
	ARG_UNUSED(prev_status);
	ARG_UNUSED(rc);
	ARG_UNUSED(group);
	ARG_UNUSED(abort_more);
	ARG_UNUSED(data);
	ARG_UNUSED(data_size);

	if (event == MGMT_EVT_OP_OS_MGMT_RESET) {
		/* MCUmgr's reset calls sys_reboot() directly: flush lazy writes first. */
		LOG_INF("BLE OTA reset requested");
		zephcore_persist_before_off();
	}
	return MGMT_CB_OK;
}

static struct mgmt_callback img_cb = {
	.callback = img_event_cb,
	.event_id = MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK | MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED |
		    MGMT_EVT_OP_IMG_MGMT_DFU_PENDING,
};

static struct mgmt_callback os_cb = {
	.callback = os_event_cb,
	.event_id = MGMT_EVT_OP_OS_MGMT_RESET,
};

void ble_ota_init(void)
{
	if (!boot_is_img_confirmed()) {
		int ret = boot_write_img_confirmed();

		if (ret) {
			LOG_ERR("MCUboot image confirm failed: %d", ret);
		} else {
			LOG_INF("MCUboot image confirmed");
		}
	}

	mgmt_callback_register(&img_cb);
	mgmt_callback_register(&os_cb);

	/* MCUmgr registered the service during init; keep it out until `start ota`. */
	int err = smp_bt_unregister();

	if (err && err != -ENOENT) {
		LOG_ERR("SMP service unregister failed: %d", err);
	}
}

int ble_ota_start(void)
{
	int err;

	k_mutex_lock(&ota_lock, K_FOREVER);
	if (atomic_get(&ota_active)) {
		k_mutex_unlock(&ota_lock);
		return -EALREADY;
	}
	err = smp_bt_register();
	if (err == 0 || err == -EALREADY) {
		atomic_set(&ota_active, 1);
		k_work_reschedule(&ota_close_work, K_SECONDS(CONFIG_ZEPHCORE_BLE_OTA_WINDOW_S));
		LOG_INF("BLE OTA window open (%u s)", CONFIG_ZEPHCORE_BLE_OTA_WINDOW_S);
		err = 0;
	} else {
		LOG_ERR("SMP service register failed: %d", err);
	}
	k_mutex_unlock(&ota_lock);
	return err;
}

int ble_ota_stop(void)
{
	k_mutex_lock(&ota_lock, K_FOREVER);
	if (!atomic_get(&ota_active)) {
		k_mutex_unlock(&ota_lock);
		return -EALREADY;
	}
	atomic_set(&ota_active, 0);
	(void)k_work_cancel_delayable(&ota_close_work);
	int err = smp_bt_unregister();

	if (err) {
		LOG_ERR("SMP service unregister failed: %d", err);
	}
	LOG_INF("BLE OTA window closed");
	k_mutex_unlock(&ota_lock);
	return 0;
}

bool ble_ota_is_active(void)
{
	return atomic_get(&ota_active) != 0;
}

uint32_t ble_ota_remaining_s(void)
{
	if (!atomic_get(&ota_active)) {
		return 0;
	}
	return (uint32_t)(k_ticks_to_ms_floor64(
		k_work_delayable_remaining_get(&ota_close_work)) / 1000U);
}
