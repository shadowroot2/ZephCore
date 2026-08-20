/* SPDX-License-Identifier: MIT */

#include "BridgeTransport.h"
#include "RepeaterBridge.h"
#include "RepeaterDataStore.h"

#include <mesh/Dispatcher.h>
#include <mesh/Packet.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(zephcore_ble_bridge, CONFIG_ZEPHCORE_MAIN_LOG_LEVEL);

namespace {

constexpr uint32_t BRIDGE_MAGIC = 0x5A434252; /* ZCBR */
constexpr uint8_t BRIDGE_VERSION = 1;
constexpr size_t BRIDGE_RAW_MAX = 234; /* 10-byte header + ATT payload <= 244 */
constexpr size_t SEEN_SLOTS = 24;
constexpr int64_t SEEN_WINDOW_MS = 120000;
constexpr uint8_t CONTROL_MAGIC[] = { 'Z', 'C', 'B', 'P' };
constexpr uint8_t CONTROL_PING = 1;
constexpr uint8_t CONTROL_PONG = 2;
constexpr uint8_t CONTROL_OBSERVED = 3;
constexpr size_t CONTROL_DATA_LEN = 8;
constexpr size_t CONTROL_LEN = sizeof(CONTROL_MAGIC) + 1 + CONTROL_DATA_LEN;
constexpr uint32_t PING_TIMEOUT_MS = 1500;
constexpr uint32_t CONNECT_TIMEOUT_MS = 12000;
constexpr uint32_t SETUP_TIMEOUT_MS = 15000;
constexpr uint32_t RETRY_MS = 1500;
constexpr uint32_t TRANSPORT_REFRESH_MS = 30000;
constexpr uint32_t HEALTH_INTERVAL_MS = 30000;
constexpr uint32_t HEALTH_TIMEOUT_MS = 6000;

#define ZEPHCORE_BRIDGE_SERVICE_UUID \
	BT_UUID_128_ENCODE(0x7c6462e1, 0x7a4f, 0x4765, 0x98cf, 0x2cd2f1a65001)
#define ZEPHCORE_BRIDGE_DATA_UUID \
	BT_UUID_128_ENCODE(0x7c6462e1, 0x7a4f, 0x4765, 0x98cf, 0x2cd2f1a65002)

static const struct bt_uuid_128 bridge_service_uuid = BT_UUID_INIT_128(ZEPHCORE_BRIDGE_SERVICE_UUID);
static const struct bt_uuid_128 bridge_data_uuid = BT_UUID_INIT_128(ZEPHCORE_BRIDGE_DATA_UUID);
static const struct bt_data bridge_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, ZEPHCORE_BRIDGE_SERVICE_UUID),
};

struct __packed BridgeFrame {
	uint32_t magic;
	uint32_t hash;
	uint8_t version;
	uint8_t raw_len;
	uint8_t raw[BRIDGE_RAW_MAX];
};
static_assert(sizeof(BridgeFrame) == 244, "BLE bridge frame must fit one ATT write");

struct SeenFrame {
	uint32_t hash;
	int64_t at_ms;
};

K_MSGQ_DEFINE(ble_bridge_rx_queue, sizeof(BridgeFrame), 4, 4);

static RepeaterDataStore *s_store;
static mesh::Dispatcher *s_dispatcher;
static RepeaterBridgePrefs s_prefs;
static bt_addr_le_t s_local_addr;
static struct bt_conn *s_conn;
static struct bt_gatt_discover_params s_discover;
static struct bt_gatt_subscribe_params s_subscribe;
static uint16_t s_service_end_handle;
static uint16_t s_peer_value_handle;
static SeenFrame s_seen[SEEN_SLOTS];
static struct k_spinlock s_seen_lock;
static struct k_spinlock s_prefs_lock;
static struct k_spinlock s_state_lock;
static bool s_started;
static bool s_is_central;
static bool s_adv_running;
static bool s_link_ready;
static int64_t s_connect_deadline_ms;
static int64_t s_setup_deadline_ms;
static int64_t s_retry_at_ms;
static int64_t s_transport_refresh_ms;
static int64_t s_health_due_ms;
static int64_t s_health_deadline_ms;
static atomic_t s_tx_count;
static atomic_t s_rx_count;
static atomic_t s_drop_count;
static atomic_t s_ping_sequence;
static atomic_t s_ping_token;
static atomic_t s_ping_sent_ms;
static atomic_t s_ping_rtt_ms;
static atomic_t s_health_sequence;
static atomic_t s_health_token;
K_SEM_DEFINE(ble_bridge_ping_sem, 0, 1);

static const uint8_t s_default_lmk[16] = {
	0x5A, 0x43, 0x42, 0x52, 0x49, 0x44, 0x47, 0x45,
	0x2D, 0x31, 0x2E, 0x31, 0x36, 0x2E, 0x38, 0x21,
};

static bool mac_is_set(const uint8_t mac[6])
{
	bool all_zero = true;
	bool all_ff = true;
	for (size_t i = 0; i < 6; ++i) {
		all_zero &= mac[i] == 0;
		all_ff &= mac[i] == 0xFF;
	}
	return !all_zero && !all_ff;
}

static void schedule_retry(uint32_t delay_ms = RETRY_MS)
{
	k_spinlock_key_t key = k_spin_lock(&s_state_lock);
	s_retry_at_ms = k_uptime_get() + delay_ms;
	k_spin_unlock(&s_state_lock, key);
}

static struct bt_conn *link_conn_ref(bool require_ready, bool *is_central,
		uint16_t *peer_value_handle)
{
	struct bt_conn *conn = nullptr;
	k_spinlock_key_t key = k_spin_lock(&s_state_lock);
	if (s_conn && (!require_ready || s_link_ready)) {
		conn = bt_conn_ref(s_conn);
		if (is_central) *is_central = s_is_central;
		if (peer_value_handle) *peer_value_handle = s_peer_value_handle;
	}
	k_spin_unlock(&s_state_lock, key);
	return conn;
}

static bool link_has_conn()
{
	k_spinlock_key_t key = k_spin_lock(&s_state_lock);
	const bool present = s_conn != nullptr;
	k_spin_unlock(&s_state_lock, key);
	return present;
}

static struct bt_conn *detach_link(const struct bt_conn *expected = nullptr)
{
	k_spinlock_key_t key = k_spin_lock(&s_state_lock);
	if (expected && s_conn && expected != s_conn) {
		k_spin_unlock(&s_state_lock, key);
		return nullptr;
	}
	struct bt_conn *conn = s_conn;
	s_conn = nullptr;
	s_link_ready = false;
	s_peer_value_handle = 0;
	s_connect_deadline_ms = 0;
	s_setup_deadline_ms = 0;
	s_health_due_ms = 0;
	s_health_deadline_ms = 0;
	atomic_set(&s_health_token, 0);
	k_spin_unlock(&s_state_lock, key);
	return conn;
}

static void reset_link(const struct bt_conn *expected = nullptr)
{
	struct bt_conn *conn = detach_link(expected);
	if (conn) {
		(void)bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		bt_conn_unref(conn);
	}
	s_adv_running = false;
	schedule_retry();
}

static void format_mac(char *out, size_t out_len, const uint8_t mac[6])
{
	snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
		 mac[3], mac[4], mac[5]);
}

static const char *addr_type_name(uint8_t type)
{
	return type == BT_ADDR_LE_RANDOM ? "random" : "public";
}

static bool parse_peer(const char *text, uint8_t mac[6], uint8_t *addr_type)
{
	unsigned int bytes[6];
	char type[8] = {};
	if (sscanf(text, "%02x:%02x:%02x:%02x:%02x:%02x %7s", &bytes[0], &bytes[1], &bytes[2],
		   &bytes[3], &bytes[4], &bytes[5], type) < 6) return false;
	/* A bare MAC stays compatible with existing setup instructions. */
	if (type[0] == '\0') {
		*addr_type = BT_ADDR_LE_PUBLIC;
	} else if (strcmp(type, "public") == 0) {
		*addr_type = BT_ADDR_LE_PUBLIC;
	} else if (strcmp(type, "random") == 0) {
		*addr_type = BT_ADDR_LE_RANDOM;
	} else {
		return false;
	}
	for (size_t i = 0; i < 6; ++i) mac[i] = (uint8_t)bytes[i];
	return mac_is_set(mac);
}

static bool parse_key(const char *text, uint8_t key[16])
{
	if (strlen(text) != 32) return false;
	for (size_t i = 0; i < 16; ++i) {
		unsigned int value;
		if (sscanf(&text[i * 2], "%02x", &value) != 1) return false;
		key[i] = (uint8_t)value;
	}
	return true;
}

static uint32_t frame_hash(const uint8_t *raw, size_t len)
{
	uint32_t hash = 2166136261u;
	uint8_t key[sizeof(s_prefs.lmk)];
	k_spinlock_key_t lock_key = k_spin_lock(&s_prefs_lock);
	memcpy(key, s_prefs.lmk, sizeof(key));
	k_spin_unlock(&s_prefs_lock, lock_key);
	for (uint8_t byte : key) {
		hash ^= byte;
		hash *= 16777619u;
	}
	for (size_t i = 0; i < len; ++i) {
		hash ^= raw[i];
		hash *= 16777619u;
	}
	return hash;
}

static bool seen_or_remember(uint32_t hash)
{
	k_spinlock_key_t key = k_spin_lock(&s_seen_lock);
	const int64_t now = k_uptime_get();
	for (const auto &entry : s_seen) {
		if (entry.hash == hash && now - entry.at_ms < SEEN_WINDOW_MS) {
			k_spin_unlock(&s_seen_lock, key);
			return true;
		}
	}
	size_t slot = 0;
	for (size_t i = 0; i < SEEN_SLOTS; ++i) {
		if (s_seen[i].hash == hash) {
			s_seen[i].at_ms = now;
			k_spin_unlock(&s_seen_lock, key);
			return false;
		}
		if (s_seen[i].at_ms < s_seen[slot].at_ms) slot = i;
	}
	s_seen[slot] = {hash, now};
	k_spin_unlock(&s_seen_lock, key);
	return false;
}

static bool connection_is_peer(const struct bt_conn *conn)
{
	const bt_addr_le_t *peer = bt_conn_get_dst(conn);
	uint8_t peer_mac[sizeof(s_prefs.peer_mac)];
	uint8_t peer_addr_type;
	k_spinlock_key_t lock_key = k_spin_lock(&s_prefs_lock);
	memcpy(peer_mac, s_prefs.peer_mac, sizeof(peer_mac));
	peer_addr_type = s_prefs.peer_addr_type;
	k_spin_unlock(&s_prefs_lock, lock_key);
	return peer && mac_is_set(peer_mac) && peer->type == peer_addr_type &&
	       memcmp(peer->a.val, peer_mac, sizeof(peer_mac)) == 0;
}

static bool send_control(uint8_t op, const uint8_t data[CONTROL_DATA_LEN]);

static bool handle_control(const BridgeFrame &frame)
{
	uint32_t token;

	if (frame.raw_len != CONTROL_LEN ||
		memcmp(frame.raw, CONTROL_MAGIC, sizeof(CONTROL_MAGIC)) != 0) return false;
	const uint8_t *data = &frame.raw[sizeof(CONTROL_MAGIC) + 1];
	memcpy(&token, data, sizeof(token));
	if (frame.raw[sizeof(CONTROL_MAGIC)] == CONTROL_PING) {
		uint8_t reply[CONTROL_DATA_LEN] = {};
		memcpy(reply, &token, sizeof(token));
		send_control(CONTROL_PONG, reply);
	} else if (frame.raw[sizeof(CONTROL_MAGIC)] == CONTROL_PONG &&
		   token == (uint32_t)atomic_get(&s_ping_token)) {
		atomic_set(&s_ping_rtt_ms, (uint32_t)k_uptime_get() -
				      (uint32_t)atomic_get(&s_ping_sent_ms));
		atomic_set(&s_ping_token, 0);
		k_sem_give(&ble_bridge_ping_sem);
	} else if (frame.raw[sizeof(CONTROL_MAGIC)] == CONTROL_PONG &&
		   token == (uint32_t)atomic_get(&s_health_token)) {
		atomic_set(&s_health_token, 0);
		k_spinlock_key_t lock_key = k_spin_lock(&s_state_lock);
		s_health_deadline_ms = 0;
		s_health_due_ms = k_uptime_get() + HEALTH_INTERVAL_MS;
		k_spin_unlock(&s_state_lock, lock_key);
	} else if (frame.raw[sizeof(CONTROL_MAGIC)] == CONTROL_OBSERVED) {
		repeater_bridge_peer_observed(data);
	}
	return true;
}

static bool accept_frame(struct bt_conn *conn, const void *data, uint16_t len)
{
	if (!connection_is_peer(conn) || bt_conn_get_security(conn) < BT_SECURITY_L2 ||
		len < offsetof(BridgeFrame, raw)) return false;
	BridgeFrame frame{};
	memcpy(&frame, data, len > sizeof(frame) ? sizeof(frame) : len);
	if (frame.magic != BRIDGE_MAGIC || frame.version != BRIDGE_VERSION || frame.raw_len < 2 ||
		frame.raw_len > BRIDGE_RAW_MAX || len != offsetof(BridgeFrame, raw) + frame.raw_len ||
		frame.hash != frame_hash(frame.raw, frame.raw_len)) {
		atomic_inc(&s_drop_count);
		return false;
	}
	if (handle_control(frame)) {
		k_spinlock_key_t lock_key = k_spin_lock(&s_state_lock);
		s_health_due_ms = k_uptime_get() + HEALTH_INTERVAL_MS;
		k_spin_unlock(&s_state_lock, lock_key);
		atomic_inc(&s_rx_count);
		return true;
	}
	if (seen_or_remember(frame.hash)) return true;
	if (k_msgq_put(&ble_bridge_rx_queue, &frame, K_NO_WAIT) != 0) {
		atomic_inc(&s_drop_count);
		return false;
	}
	atomic_inc(&s_rx_count);
	k_spinlock_key_t lock_key = k_spin_lock(&s_state_lock);
	s_health_due_ms = k_uptime_get() + HEALTH_INTERVAL_MS;
	k_spin_unlock(&s_state_lock, lock_key);
	if (s_dispatcher) s_dispatcher->notifyWake();
	return true;
}

static ssize_t bridge_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);
	if (offset != 0 || !accept_frame(conn, buf, len)) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	return len;
}

static void bridge_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	k_spinlock_key_t lock_key = k_spin_lock(&s_state_lock);
	s_link_ready = value == BT_GATT_CCC_NOTIFY && s_conn != nullptr;
	s_setup_deadline_ms = s_link_ready ? 0 : k_uptime_get() + SETUP_TIMEOUT_MS;
	s_health_due_ms = s_link_ready ? k_uptime_get() + HEALTH_INTERVAL_MS : 0;
	k_spin_unlock(&s_state_lock, lock_key);
}

BT_GATT_SERVICE_DEFINE(bridge_service,
	BT_GATT_PRIMARY_SERVICE(&bridge_service_uuid),
	BT_GATT_CHARACTERISTIC(&bridge_data_uuid.uuid,
		BT_GATT_CHRC_WRITE_WITHOUT_RESP | BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_WRITE_ENCRYPT, NULL, bridge_write, NULL),
	BT_GATT_CCC(bridge_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT));

static bool send_control(uint8_t op, const uint8_t data[CONTROL_DATA_LEN])
{
	bool is_central;
	uint16_t peer_value_handle;
	struct bt_conn *conn = link_conn_ref(true, &is_central, &peer_value_handle);
	if (!conn) return false;
	BridgeFrame frame{};

	frame.magic = BRIDGE_MAGIC;
	frame.version = BRIDGE_VERSION;
	frame.raw_len = CONTROL_LEN;
	memcpy(frame.raw, CONTROL_MAGIC, sizeof(CONTROL_MAGIC));
	frame.raw[sizeof(CONTROL_MAGIC)] = op;
	memcpy(&frame.raw[sizeof(CONTROL_MAGIC) + 1], data, CONTROL_DATA_LEN);
	frame.hash = frame_hash(frame.raw, frame.raw_len);
	int err = is_central ?
		bt_gatt_write_without_response(conn, peer_value_handle, &frame,
					       offsetof(BridgeFrame, raw) + frame.raw_len, false) :
		bt_gatt_notify(conn, &bridge_service.attrs[2], &frame,
			       offsetof(BridgeFrame, raw) + frame.raw_len);
	bt_conn_unref(conn);
	if (err != 0) {
		LOG_WRN("BLE bridge control send failed: %d", err);
		return false;
	}
	atomic_inc(&s_tx_count);
	return true;
}

static uint8_t notification_cb(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
				   const void *data, uint16_t len)
{
	if (!data) {
		params->value_handle = 0;
		/* A subscription can disappear while the ACL link is still nominally
		 * alive. Keeping s_conn in that state permanently blocks scan/advertise. */
		reset_link(conn);
		return BT_GATT_ITER_STOP;
	}
	accept_frame(conn, data, len);
	return BT_GATT_ITER_CONTINUE;
}

static uint8_t discovery_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				struct bt_gatt_discover_params *params)
{
	if (!attr) {
		memset(params, 0, sizeof(*params));
		if (link_has_conn()) {
			LOG_WRN("BLE bridge service discovery incomplete");
			bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		}
		return BT_GATT_ITER_STOP;
	}
	if (!bt_uuid_cmp(params->uuid, &bridge_service_uuid.uuid)) {
		auto *service = static_cast<const struct bt_gatt_service_val *>(attr->user_data);
		s_service_end_handle = service->end_handle;
		params->uuid = &bridge_data_uuid.uuid;
		params->start_handle = attr->handle + 1;
		params->end_handle = s_service_end_handle;
		params->type = BT_GATT_DISCOVER_CHARACTERISTIC;
		if (bt_gatt_discover(conn, params) != 0) {
			LOG_WRN("BLE bridge characteristic discovery could not start");
			bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		}
		return BT_GATT_ITER_STOP;
	}
	if (!bt_uuid_cmp(params->uuid, &bridge_data_uuid.uuid)) {
	k_spinlock_key_t lock_key = k_spin_lock(&s_state_lock);
	s_peer_value_handle = bt_gatt_attr_value_handle(attr);
	k_spin_unlock(&s_state_lock, lock_key);
		params->uuid = BT_UUID_GATT_CCC;
		params->start_handle = attr->handle + 2;
		params->end_handle = s_service_end_handle;
		params->type = BT_GATT_DISCOVER_DESCRIPTOR;
		if (bt_gatt_discover(conn, params) != 0) {
			LOG_WRN("BLE bridge CCC discovery could not start");
			bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		}
		return BT_GATT_ITER_STOP;
	}

	memset(&s_subscribe, 0, sizeof(s_subscribe));
	s_subscribe.value_handle = bt_gatt_attr_value_handle(attr);
	s_subscribe.ccc_handle = attr->handle;
	s_subscribe.value = BT_GATT_CCC_NOTIFY;
	s_subscribe.notify = notification_cb;
	s_subscribe.min_security = BT_SECURITY_L2;
	int err = bt_gatt_subscribe(conn, &s_subscribe);
	if (err == 0 || err == -EALREADY) {
		k_spinlock_key_t lock_key = k_spin_lock(&s_state_lock);
		s_link_ready = true;
		s_setup_deadline_ms = 0;
		s_health_due_ms = k_uptime_get() + HEALTH_INTERVAL_MS;
		k_spin_unlock(&s_state_lock, lock_key);
	} else {
		LOG_WRN("BLE bridge subscribe failed: %d", err);
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
	return BT_GATT_ITER_STOP;
}

static void start_scan();
static void start_advertising();

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		/* bt_conn_le_create() stops scanning.  A transient connection failure
		 * must not leave the central permanently in "waiting". */
		schedule_retry();
		if (s_started && mac_is_set(s_prefs.peer_mac)) {
			if (s_is_central) start_scan();
			else {
				s_adv_running = false;
				start_advertising();
			}
		}
		return;
	}
	if (!connection_is_peer(conn)) {
		/* Connectable advertising stops as soon as any central connects.  Do
		 * not let a Configurator or scanner permanently hide the peripheral. */
		if (!s_is_central) s_adv_running = false;
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		return;
	}
	k_spinlock_key_t lock_key = k_spin_lock(&s_state_lock);
	s_conn = bt_conn_ref(conn);
	s_adv_running = false;
	s_link_ready = false;
	s_peer_value_handle = 0;
	s_connect_deadline_ms = 0;
	s_setup_deadline_ms = k_uptime_get() + SETUP_TIMEOUT_MS;
	s_health_due_ms = 0;
	s_health_deadline_ms = 0;
	k_spin_unlock(&s_state_lock, lock_key);
	int sec_err = bt_conn_set_security(conn, BT_SECURITY_L2);
	if (sec_err && sec_err != -EALREADY) {
		LOG_WRN("BLE bridge security request failed: %d", sec_err);
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(reason);
	struct bt_conn *current = link_conn_ref(false, nullptr, nullptr);
	if (current && conn != current) {
		bt_conn_unref(current);
		return;
	}
	if (current) bt_conn_unref(current);
	struct bt_conn *owned = detach_link(conn);
	if (owned) bt_conn_unref(owned);
	/* A rejected foreign connection has no s_conn reference, but it still
	 * stopped advertising.  Re-enter the selected role in both cases. */
	s_adv_running = false;
	s_link_ready = false;
	s_peer_value_handle = 0;
	schedule_retry();
	if (s_started && mac_is_set(s_prefs.peer_mac)) {
		if (s_is_central) start_scan();
		else start_advertising();
	}
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	if (!link_has_conn()) return;
	if (err || level < BT_SECURITY_L2) {
		LOG_WRN("BLE bridge security failed: %d", err);
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		return;
	}
	if (!s_is_central) return;
	memset(&s_discover, 0, sizeof(s_discover));
	s_discover.uuid = &bridge_service_uuid.uuid;
	s_discover.func = discovery_cb;
	s_discover.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	s_discover.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	s_discover.type = BT_GATT_DISCOVER_PRIMARY;
	if (bt_gatt_discover(conn, &s_discover) != 0) {
		LOG_WRN("BLE bridge service discovery could not start");
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

BT_CONN_CB_DEFINE(bridge_conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static void scan_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type, struct net_buf_simple *ad)
{
	ARG_UNUSED(rssi);
	ARG_UNUSED(ad);
	uint8_t peer_mac[sizeof(s_prefs.peer_mac)];
	uint8_t peer_addr_type;
	k_spinlock_key_t lock_key = k_spin_lock(&s_prefs_lock);
	memcpy(peer_mac, s_prefs.peer_mac, sizeof(peer_mac));
	peer_addr_type = s_prefs.peer_addr_type;
	k_spin_unlock(&s_prefs_lock, lock_key);
	if (!s_is_central || link_has_conn() ||
		(type != BT_GAP_ADV_TYPE_ADV_IND && type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) ||
		addr->type != peer_addr_type ||
		memcmp(addr->a.val, peer_mac, sizeof(peer_mac)) != 0) return;
	int stop_err = bt_le_scan_stop();
	if (stop_err != 0 && stop_err != -EALREADY) {
		start_scan();
		return;
	}
	struct bt_conn *conn = nullptr;
	int err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT, &conn);
	if (err == 0 && conn) {
		k_spinlock_key_t state_key = k_spin_lock(&s_state_lock);
		s_connect_deadline_ms = k_uptime_get() + CONNECT_TIMEOUT_MS;
		k_spin_unlock(&s_state_lock, state_key);
		bt_conn_unref(conn);
	} else {
		schedule_retry();
		start_scan();
	}
}

static void start_scan()
{
	if (!s_started || !s_is_central || link_has_conn() || !mac_is_set(s_prefs.peer_mac)) return;
	int err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, scan_found);
	if (err && err != -EALREADY) {
		LOG_WRN("BLE bridge scan failed: %d", err);
		schedule_retry();
	} else {
		k_spinlock_key_t lock_key = k_spin_lock(&s_state_lock);
		s_retry_at_ms = 0;
		s_transport_refresh_ms = k_uptime_get() + TRANSPORT_REFRESH_MS;
		k_spin_unlock(&s_state_lock, lock_key);
	}
}

static void start_advertising()
{
	if (!s_started || s_is_central || link_has_conn() || !mac_is_set(s_prefs.peer_mac) || s_adv_running) return;
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, bridge_ad, ARRAY_SIZE(bridge_ad), nullptr, 0);
	if (err == 0 || err == -EALREADY) {
		s_adv_running = true;
		k_spinlock_key_t lock_key = k_spin_lock(&s_state_lock);
		s_retry_at_ms = 0;
		s_transport_refresh_ms = k_uptime_get() + TRANSPORT_REFRESH_MS;
		k_spin_unlock(&s_state_lock, lock_key);
	}
	else {
		LOG_WRN("BLE bridge advertising failed: %d", err);
		schedule_retry();
	}
}

static bool configure_peer()
{
	if (!s_started || !mac_is_set(s_prefs.peer_mac)) return false;
	reset_link();
	bt_le_scan_stop();
	if (s_adv_running) {
		bt_le_adv_stop();
		s_adv_running = false;
	}
	s_is_central = memcmp(s_local_addr.a.val, s_prefs.peer_mac, 6) < 0;
	if (s_is_central) start_scan();
	else start_advertising();
	return true;
}

static bool save_and_configure()
{
	return s_store && s_store->saveBridgePrefs(s_prefs) &&
		(!s_started || !mac_is_set(s_prefs.peer_mac) || configure_peer());
}

} // namespace

bool ble_bridge_start(RepeaterDataStore *store, mesh::Dispatcher *dispatcher)
{
	s_store = store;
	s_dispatcher = dispatcher;
	memset(&s_prefs, 0, sizeof(s_prefs));
	if (!s_store->loadBridgePrefs(s_prefs)) {
		memcpy(s_prefs.lmk, s_default_lmk, sizeof(s_prefs.lmk));
		s_store->saveBridgePrefs(s_prefs);
	}
	int err = bt_enable(nullptr);
	if (err && err != -EALREADY) {
		LOG_ERR("BLE bridge init failed: %d", err);
		return false;
	}
	if (IS_ENABLED(CONFIG_SETTINGS)) settings_load();
	size_t count = 1;
	bt_id_get(&s_local_addr, &count);
	if (count == 0) {
		LOG_ERR("BLE bridge could not read identity address");
		return false;
	}
	s_started = true;
	{
		k_spinlock_key_t lock_key = k_spin_lock(&s_state_lock);
		s_connect_deadline_ms = 0;
		s_setup_deadline_ms = 0;
		s_retry_at_ms = 0;
		s_transport_refresh_ms = 0;
		s_health_due_ms = 0;
		s_health_deadline_ms = 0;
		k_spin_unlock(&s_state_lock, lock_key);
	}
	if (mac_is_set(s_prefs.peer_mac)) configure_peer();
	return true;
}

bool ble_bridge_get_local_mac(char *out, size_t out_len)
{
	if (!out || out_len < 18 || !s_started) return false;
	format_mac(out, out_len, s_local_addr.a.val);
	return true;
}

bool ble_bridge_handle_command(const char *command, char *reply, size_t reply_len)
{
	if (!command || !reply || reply_len == 0) return false;
	if (strcmp(command, "get bridge.mac") == 0 || strcmp(command, "get bridge.peer") == 0) {
		char local[26] = "unavailable";
		char peer[26] = "not set";
		char mac[18];
		if (s_started) {
			format_mac(mac, sizeof(mac), s_local_addr.a.val);
			snprintf(local, sizeof(local), "%s %s", mac, addr_type_name(s_local_addr.type));
		}
		if (mac_is_set(s_prefs.peer_mac)) {
			format_mac(mac, sizeof(mac), s_prefs.peer_mac);
			snprintf(peer, sizeof(peer), "%s %s", mac, addr_type_name(s_prefs.peer_addr_type));
		}
		snprintf(reply, reply_len, "bridge: %s; transport=BLE; local=%s; peer=%s; encrypted; key=%s; tx=%u rx=%u drop=%u",
			ble_bridge_is_connected() ? "ready" : (s_started ? "waiting" : "offline"), local, peer,
			s_prefs.key_is_custom ? "custom" : "default", (unsigned int)atomic_get(&s_tx_count),
			(unsigned int)atomic_get(&s_rx_count), (unsigned int)atomic_get(&s_drop_count));
		return true;
	}
	const char *peer_value = nullptr;
	if (strncmp(command, "set bridge ", 11) == 0) peer_value = command + 11;
	if (strncmp(command, "set bridge.peer ", 16) == 0) peer_value = command + 16;
	if (peer_value) {
		uint8_t peer[6];
		uint8_t peer_addr_type;
		if (!parse_peer(peer_value, peer, &peer_addr_type)) {
			snprintf(reply, reply_len, "ERR: use set bridge.peer MAC [public|random]");
			return true;
		}
		k_spinlock_key_t lock_key = k_spin_lock(&s_prefs_lock);
		memcpy(s_prefs.peer_mac, peer, sizeof(peer));
		s_prefs.peer_addr_type = peer_addr_type;
		k_spin_unlock(&s_prefs_lock, lock_key);
		snprintf(reply, reply_len, save_and_configure() ? "OK: bridge peer saved" : "ERR: bridge peer not saved");
		return true;
	}
	if (strncmp(command, "set bridge.key ", 15) == 0) {
		uint8_t key[16];
		if (!parse_key(command + 15, key)) {
			snprintf(reply, reply_len, "ERR: bridge.key needs 32 hex characters");
			return true;
		}
		k_spinlock_key_t lock_key = k_spin_lock(&s_prefs_lock);
		memcpy(s_prefs.lmk, key, sizeof(key));
		s_prefs.key_is_custom = 1;
		k_spin_unlock(&s_prefs_lock, lock_key);
		snprintf(reply, reply_len, save_and_configure() ? "OK: bridge key saved; set same key on peer" : "ERR: bridge key not saved");
		return true;
	}
	return false;
}

bool ble_bridge_ping(char *reply, size_t reply_len)
{
	if (!reply || reply_len == 0) return false;
	if (!s_started || !ble_bridge_is_connected()) {
		snprintf(reply, reply_len, "ERR: bridge peer is not connected");
		return true;
	}
	while (k_sem_take(&ble_bridge_ping_sem, K_NO_WAIT) == 0) {}
	uint32_t token = (uint32_t)atomic_inc(&s_ping_sequence) + 1;
	if (token == 0) token = (uint32_t)atomic_inc(&s_ping_sequence) + 1;
	atomic_set(&s_ping_token, token);
	atomic_set(&s_ping_sent_ms, (uint32_t)k_uptime_get());
	uint8_t ping_data[CONTROL_DATA_LEN] = {};
	memcpy(ping_data, &token, sizeof(token));
	if (!send_control(CONTROL_PING, ping_data)) {
		atomic_set(&s_ping_token, 0);
		reset_link();
		snprintf(reply, reply_len, "ERR: bridge ping send failed");
		return true;
	}
	if (k_sem_take(&ble_bridge_ping_sem, K_MSEC(PING_TIMEOUT_MS)) == 0) {
		snprintf(reply, reply_len, "OK: bridge pong %ums",
			 (unsigned int)atomic_get(&s_ping_rtt_ms));
	} else {
		atomic_set(&s_ping_token, 0);
		reset_link();
		snprintf(reply, reply_len, "ERR: bridge ping timeout");
	}
	return true;
}

bool ble_bridge_forward_packet(const mesh::Packet *packet)
{
	bool is_central;
	uint16_t peer_value_handle;
	struct bt_conn *conn = link_conn_ref(true, &is_central, &peer_value_handle);
	if (!conn || !packet) {
		if (conn) bt_conn_unref(conn);
		return false;
	}
	const int raw_len = packet->getRawLength();
	if (raw_len < 2 || raw_len > (int)BRIDGE_RAW_MAX) {
		if (raw_len > (int)BRIDGE_RAW_MAX) {
			atomic_inc(&s_drop_count);
			LOG_WRN("BLE bridge skips %d-byte packet", raw_len);
		}
		bt_conn_unref(conn);
		return false;
	}
	BridgeFrame frame{};
	frame.magic = BRIDGE_MAGIC;
	frame.version = BRIDGE_VERSION;
	frame.raw_len = raw_len;
	packet->writeTo(frame.raw);
	frame.hash = frame_hash(frame.raw, frame.raw_len);
	if (seen_or_remember(frame.hash)) {
		bt_conn_unref(conn);
		return false;
	}
	int err;
	if (is_central) {
		if (!peer_value_handle) {
			bt_conn_unref(conn);
			return false;
		}
		err = bt_gatt_write_without_response(conn, peer_value_handle, &frame,
					     offsetof(BridgeFrame, raw) + frame.raw_len, false);
	} else {
		err = bt_gatt_notify(conn, &bridge_service.attrs[2], &frame,
				     offsetof(BridgeFrame, raw) + frame.raw_len);
	}
	bt_conn_unref(conn);
	if (err != 0) {
		LOG_WRN("BLE bridge send failed: %d", err);
		return false;
	}
	atomic_inc(&s_tx_count);
	return true;
}

bool ble_bridge_send_observed(const uint8_t fingerprint[8])
{
	return fingerprint && send_control(CONTROL_OBSERVED, fingerprint);
}

void ble_bridge_drain(mesh::Dispatcher *dispatcher)
{
	BridgeFrame frame;
	while (k_msgq_get(&ble_bridge_rx_queue, &frame, K_NO_WAIT) == 0) {
		repeater_bridge_note_inbound_raw(frame.raw, frame.raw_len);
		dispatcher->injectRaw(frame.raw, frame.raw_len);
	}
}

void ble_bridge_maintain(void)
{
	if (!s_started || !mac_is_set(s_prefs.peer_mac)) return;
	const int64_t now = k_uptime_get();
	bool is_central;
	bool link_ready;
	bool has_conn;
	bool start_health_ping = false;
	bool reset = false;
	bool refresh_transport = false;

	k_spinlock_key_t lock_key = k_spin_lock(&s_state_lock);
	is_central = s_is_central;
	has_conn = s_conn != nullptr;
	link_ready = s_link_ready;
	if (!has_conn) {
		if ((s_connect_deadline_ms && now >= s_connect_deadline_ms) ||
		    (s_retry_at_ms && now >= s_retry_at_ms) ||
		    (s_transport_refresh_ms && now >= s_transport_refresh_ms)) {
			s_connect_deadline_ms = 0;
			s_retry_at_ms = 0;
			refresh_transport = true;
		}
	} else if (!link_ready && s_setup_deadline_ms && now >= s_setup_deadline_ms) {
		reset = true;
	} else if (link_ready && s_health_deadline_ms && now >= s_health_deadline_ms) {
		reset = true;
	} else if (link_ready && !atomic_get(&s_health_token) && s_health_due_ms && now >= s_health_due_ms) {
		uint32_t token = (uint32_t)atomic_inc(&s_health_sequence) + 1;
		if (token == 0) token = (uint32_t)atomic_inc(&s_health_sequence) + 1;
		atomic_set(&s_health_token, token);
		s_health_due_ms = 0;
		s_health_deadline_ms = now + HEALTH_TIMEOUT_MS;
		start_health_ping = true;
	}
	k_spin_unlock(&s_state_lock, lock_key);

	if (reset) {
		LOG_WRN("BLE bridge link watchdog reconnecting");
		reset_link();
		return;
	}
	if (refresh_transport) {
		if (is_central) start_scan();
		else {
			if (s_adv_running) {
				(void)bt_le_adv_stop();
				s_adv_running = false;
			}
			start_advertising();
		}
		return;
	}
	if (start_health_ping) {
		uint8_t data[CONTROL_DATA_LEN] = {};
		const uint32_t token = (uint32_t)atomic_get(&s_health_token);
		memcpy(data, &token, sizeof(token));
		if (!send_control(CONTROL_PING, data)) {
			LOG_WRN("BLE bridge health ping send failed");
			reset_link();
		}
	}
}

uint32_t ble_bridge_ms_until_next(void)
{
	if (!s_started || !mac_is_set(s_prefs.peer_mac)) return UINT32_MAX;
	const int64_t now = k_uptime_get();
	int64_t deadline = 0;
	k_spinlock_key_t lock_key = k_spin_lock(&s_state_lock);
	const int64_t candidates[] = { s_connect_deadline_ms, s_setup_deadline_ms,
		s_retry_at_ms, s_transport_refresh_ms, s_health_due_ms, s_health_deadline_ms };
	for (const int64_t candidate : candidates) {
		if (candidate && (!deadline || candidate < deadline)) deadline = candidate;
	}
	k_spin_unlock(&s_state_lock, lock_key);
	if (!deadline || deadline <= now) return deadline ? 0 : UINT32_MAX;
	const int64_t delta = deadline - now;
	return delta > INT32_MAX ? UINT32_MAX : (uint32_t)delta;
}

void ble_bridge_stop()
{
	if (!s_started) return;
	bt_le_scan_stop();
	if (s_adv_running) bt_le_adv_stop();
	struct bt_conn *conn = detach_link();
	if (conn) {
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		bt_conn_unref(conn);
	}
	s_adv_running = false;
	s_link_ready = false;
	s_started = false;
	s_dispatcher = nullptr;
	k_msgq_purge(&ble_bridge_rx_queue);
}

bool ble_bridge_is_connected(void)
{
	k_spinlock_key_t lock_key = k_spin_lock(&s_state_lock);
	const bool connected = s_started && s_link_ready && s_conn != nullptr;
	k_spin_unlock(&s_state_lock, lock_key);
	return connected;
}
