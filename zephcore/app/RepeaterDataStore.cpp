/*
 * SPDX-License-Identifier: MIT
 * RepeaterDataStore - Filesystem storage for repeater
 */

#include "RepeaterDataStore.h"
#include "../adapters/datastore/ZephyrFsFormat.h"
#include "../adapters/datastore/ZephyrFsUtil.h"
#include "../adapters/datastore/IdentityFile.h"
#include "../adapters/datastore/PrefsFile.h"
#include <helpers/PrefsCodec.h>
#include <helpers/LoRaConfig.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(zephcore_repeater_store, CONFIG_ZEPHCORE_DATASTORE_LOG_LEVEL);

RepeaterDataStore::RepeaterDataStore() : _initialized(false) {
}

bool RepeaterDataStore::begin() {
	if (_initialized) return true;

	/* Create repeater directory if it doesn't exist */
	struct fs_dirent entry;
	int ret = fs_stat(BASE_PATH, &entry);
	if (ret < 0) {
		ret = fs_mkdir(BASE_PATH);
		if (ret < 0 && ret != -EEXIST) {
			LOG_ERR("Failed to create %s: %d", BASE_PATH, ret);
			return false;
		}
		LOG_INF("Created %s directory", BASE_PATH);
	}

	/* Nothing a server role stores lives on /ext. */
	zephcore_fs_ext_power_down();

	_initialized = true;
	LOG_INF("RepeaterDataStore initialized at %s", BASE_PATH);
	return true;
}

const char* RepeaterDataStore::getBasePath() const { return BASE_PATH; }

bool RepeaterDataStore::hasRoleData() const {
	char path[64];

	/* Only this role's files count: a repeater booting onto a companion volume
	 * formats it. Repeater, room server and observer share the store, so
	 * switching among them keeps the identity. */
	static const char* const ours[] = { "prefs.json", "prefs", "_main.id" };
	for (size_t i = 0; i < ARRAY_SIZE(ours); i++) {
		snprintf(path, sizeof(path), "%s/%s", BASE_PATH, ours[i]);
		if (zephcore_fs_exists(path)) return true;
	}

	return false;
}

bool RepeaterDataStore::loadIdentity(mesh::LocalIdentity& id) {
	char path[48];
	snprintf(path, sizeof(path), "%s/_main.id", BASE_PATH);
	return zephcore_identity_load(path, id);
}

bool RepeaterDataStore::saveIdentity(const mesh::LocalIdentity& id) {
	if (!_initialized) begin();

	char path[48];
	snprintf(path, sizeof(path), "%s/_main.id", BASE_PATH);
	return zephcore_identity_save(path, id);
}

bool RepeaterDataStore::loadBatteryPrefs(RepeaterBatteryPrefs& prefs) {
	prefs = RepeaterBatteryPrefs{};
	struct fs_file_t file;
	fs_file_t_init(&file);
	if (fs_open(&file, "/lfs/repeater/battery_prefs", FS_O_READ) < 0) return false;
	struct {
		uint32_t magic, enabled, hours;
		char group[32];
		uint32_t threshold_mv;
	} record = {};
	ssize_t n = fs_read(&file, &record, sizeof(record));
	fs_close(&file);
	const bool legacy = n == 12 && record.magic == 0x42504631;
	const bool version2 = n == 44 && record.magic == 0x42504632;
	if (!legacy && !version2 && (n != sizeof(record) || record.magic != 0x42504633)) return false;
	if (record.enabled > 1 || record.hours < 1 || record.hours > 168 || record.threshold_mv > 5000) return false;
	if (!legacy) {
		if (!memchr(record.group, 0, sizeof(record.group)) || record.group[0] != '#' ||
		    strchr(record.group, '\r') || strchr(record.group, '\n')) return false;
		memcpy(prefs.group_name, record.group, sizeof(prefs.group_name));
	}
	prefs.enabled = record.enabled != 0;
	prefs.interval_hours = record.hours;
	if (!legacy && !version2) prefs.threshold_mv = record.threshold_mv;
	return true;
}

bool RepeaterDataStore::saveBatteryPrefs(const RepeaterBatteryPrefs& prefs) {
	if (prefs.interval_hours < 1 || prefs.interval_hours > 168 || prefs.threshold_mv > 5000) return false;
	if (!memchr(prefs.group_name, 0, sizeof(prefs.group_name)) || prefs.group_name[0] != '#' ||
	    strchr(prefs.group_name, '\r') || strchr(prefs.group_name, '\n')) return false;
	if (!_initialized && !begin()) return false;
	static const char path[] = "/lfs/repeater/battery_prefs";
	static const char tmp[] = "/lfs/repeater/battery_prefs.tmp";
	struct fs_file_t file;
	fs_file_t_init(&file);
	if (fs_open(&file, tmp, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC) < 0) return false;
	struct {
		uint32_t magic, enabled, hours;
		char group[32];
		uint32_t threshold_mv;
	} record = {0x42504633, prefs.enabled ? 1U : 0U, prefs.interval_hours, {}, prefs.threshold_mv};
	memcpy(record.group, prefs.group_name, sizeof(record.group));
	ssize_t n = fs_write(&file, &record, sizeof(record));
	int synced = fs_sync(&file);
	int closed = fs_close(&file);
	if (n != sizeof(record) || synced < 0 || closed < 0) return false;
	return fs_rename(tmp, path) == 0;
}

bool RepeaterDataStore::loadBatteryAlertTime(uint32_t& epoch) {
	struct fs_file_t file;
	fs_file_t_init(&file);
	if (fs_open(&file, "/lfs/repeater/battery_alert", FS_O_READ) < 0) return false;
	uint32_t record[2] = {};
	ssize_t n = fs_read(&file, record, sizeof(record));
	fs_close(&file);
	if (n != sizeof(record) || record[0] != 0x42415431) return false;
	epoch = record[1];
	return true;
}

bool RepeaterDataStore::saveBatteryAlertTime(uint32_t epoch) {
	if (!_initialized && !begin()) return false;
	static const char path[] = "/lfs/repeater/battery_alert";
	static const char tmp[] = "/lfs/repeater/battery_alert.tmp";
	struct fs_file_t file;
	fs_file_t_init(&file);
	if (fs_open(&file, tmp, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC) < 0) return false;
	const uint32_t record[] = {0x42415431, epoch};
	ssize_t n = fs_write(&file, record, sizeof(record));
	int synced = fs_sync(&file);
	int closed = fs_close(&file);
	if (n != sizeof(record) || synced < 0 || closed < 0) return false;
	return fs_rename(tmp, path) == 0;
}

bool RepeaterDataStore::loadPrefs(NodePrefs& prefs) {
	char path[48];
	snprintf(path, sizeof(path), "%s/prefs.json", BASE_PATH);

	if (zephcore_prefs_json_load(path, prefs, serverPrefsFromJson)) {
		LOG_INF("Loaded prefs from %s", path);
		return true;
	}
	bool had_json = zephcore_fs_exists(path);

	/* No usable prefs.json: the legacy binary file, read once and migrated.
	 * It is kept, so firmware from before prefs.json still boots with the
	 * settings as they were at the upgrade. With neither, the caller's prefs
	 * are the role's defaults (set in its constructor). Either way they are
	 * saved, so flash always holds a prefs file from boot 1 and later code
	 * (e.g. tempradio revert) can trust it without a "first run" case. */
	char legacy[48];
	snprintf(legacy, sizeof(legacy), "%s/prefs", BASE_PATH);
	if (zephcore_fs_exists(legacy)) {
		if (loadLegacyPrefs(legacy, prefs)) {
			LOG_INF("loadPrefs: %s %s from %s", had_json ? "recovered" : "migrated",
				path, legacy);
		}
	} else {
		LOG_DBG("No prefs file at %s, saving the role defaults", path);
	}
	savePrefs(prefs);
	return true;
}

bool RepeaterDataStore::loadLegacyPrefs(const char* path, NodePrefs& prefs) {
	uint8_t buf[SERVER_PREFS_SIZE + 32];
	size_t len = 0;
	if (!zephcore_fs_read_file(path, buf, sizeof(buf), &len)) {
		LOG_ERR("loadPrefs: read of %s failed, using defaults", path);
		return false;
	}
	LOG_DBG("loadPrefs: file size = %d bytes", (int)len);

	if (len >= 120) {
		float freq, bw;
		memcpy(&freq, &buf[72], sizeof(freq));
		memcpy(&bw, &buf[116], sizeof(bw));
		if (freq < ZC_RADIO_FREQ_MIN_MHZ || freq > ZC_RADIO_FREQ_MAX_MHZ ||
		    buf[112] < 5 || buf[112] > 12 ||
		    bw < ZC_RADIO_BW_MIN_KHZ || bw > ZC_RADIO_BW_MAX_KHZ) {
			LOG_WRN("Invalid radio params in prefs, using defaults: freq=%.3f sf=%u bw=%.1f",
				(double)freq, buf[112], (double)bw);
		}
	}
	/* Radio params outside ZC_RADIO_* fall back to the LoRaConfig defaults,
	 * everything else is sanitized; fields past the end keep the caller's. */
	serverPrefsDecode(prefs, buf, len);

	LOG_DBG("  name='%s' freq=%.3f sf=%u bw=%.1f tx_pwr=%d",
		prefs.node_name, (double)prefs.freq, prefs.sf, (double)prefs.bw, prefs.tx_power_dbm);

	/* Old files (< 294 bytes) never saved the ZephCore extension fields, and
	 * stored path_hash_mode/loop_detect as zero padding: the repeater defaults. */
	if (len < 294) {
		prefs.rx_boost = 1;
		prefs.path_hash_mode = 1;
		prefs.loop_detect = LOOP_DETECT_MODERATE;
		LOG_INF("loadPrefs: upgraded an old %d-byte prefs file", (int)len);
	}

	/* Before the repeater honoured gps_interval it ran a fixed 48 h, so a
	 * stored companion default (300) was never a choice: the repeater
	 * default instead. Legacy files only; prefs.json is past this. */
	if (prefs.gps_interval == CONFIG_ZEPHCORE_GPS_POLL_INTERVAL_SEC) {
		prefs.gps_interval = CONFIG_ZEPHCORE_REPEATER_GPS_INTERVAL_SEC;
	}
	/* Likewise gps_enabled: that firmware ran the GPS whatever it said. */
	prefs.gps_enabled = 1;
	prefs.gps_enabled_set = 1;
	/* And powersaving, which it stored but never acted on. */
	prefs.powersaving_set = 0;
	powersaving_upgrade(&prefs);
	return true;
}

bool RepeaterDataStore::savePrefs(const NodePrefs& prefs) {
	if (!_initialized) begin();

	char path[48];
	snprintf(path, sizeof(path), "%s/prefs.json", BASE_PATH);
	if (!zephcore_prefs_json_save(path, prefs, serverPrefsToJson)) {
		return false;
	}
	LOG_INF("Saved prefs to %s", path);
	return true;
}

bool RepeaterDataStore::loadBridgePrefs(RepeaterBridgePrefs& prefs) {
	char path[48];
	snprintf(path, sizeof(path), "%s/espnow_bridge", BASE_PATH);
	struct fs_file_t file;
	fs_file_t_init(&file);
	if (fs_open(&file, path, FS_O_READ) < 0) return false;
	memset(&prefs, 0, sizeof(prefs));
	ssize_t n = fs_read(&file, &prefs, sizeof(prefs));
	fs_close(&file);
	constexpr size_t pre_address_type_size = offsetof(RepeaterBridgePrefs, peer_addr_type);
	constexpr size_t pre_priority_size = offsetof(RepeaterBridgePrefs, forward_priority);
	if (n == (ssize_t)pre_address_type_size || n == (ssize_t)pre_priority_size) {
		prefs.forward_priority = 7;
		LOG_INF("Migrated bridge prefs");
		return true;
	}
	if (n != (ssize_t)sizeof(prefs)) {
		LOG_WRN("Bridge prefs are corrupt");
		return false;
	}
	return true;
}

bool RepeaterDataStore::saveBridgePrefs(const RepeaterBridgePrefs& prefs) {
	if (!_initialized && !begin()) return false;
	char path[48];
	char tmp_path[56];
	snprintf(path, sizeof(path), "%s/espnow_bridge", BASE_PATH);
	if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path) >= (int)sizeof(tmp_path)) return false;
	fs_unlink(tmp_path);
	struct fs_file_t file;
	fs_file_t_init(&file);
	if (fs_open(&file, tmp_path, FS_O_CREATE | FS_O_WRITE) < 0) return false;
	ssize_t n = fs_write(&file, &prefs, sizeof(prefs));
	int ret = fs_sync(&file);
	fs_close(&file);
	if (n != (ssize_t)sizeof(prefs) || ret < 0 || fs_rename(tmp_path, path) < 0) {
		fs_unlink(tmp_path);
		return false;
	}
	return true;
}

bool RepeaterDataStore::formatFileSystem() {
	LOG_WRN("Factory reset: erasing all storage");

	/* Erase the LittleFS *volume*, not just our files.  The old loop walked
	 * /lfs/repeater/ with fs_unlink, which left the volume itself untouched:
	 * it could not recover a volume another firmware had written into (on
	 * nRF52840 the Adafruit core's filesystem overlaps the top of ours), and
	 * it left /lfs/settings, stale companion files and all of /ext behind.
	 * Shared with the companion so all four roles erase the same regions. */
	bool mounted = zephcore_fs_format_all(nullptr);
	if (!mounted) {
		LOG_ERR("Factory reset: /lfs did not remount");
		return false;
	}

	/* The format took /lfs/repeater with it.  Re-create it now rather than
	 * relying on the reboot: the CLI defers the reset so the reply can be
	 * transmitted, and anything that saves in that window needs the dir. */
	_initialized = false;
	if (!begin()) {
		LOG_ERR("Factory reset: could not re-create %s", BASE_PATH);
		return false;
	}

	LOG_INF("Repeater data erased");
	return true;
}
