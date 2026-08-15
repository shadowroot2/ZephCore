/* SPDX-License-Identifier: MIT */

#include "BridgeTransport.h"
#include "RepeaterBridge.h"
#include "RepeaterDataStore.h"

#include <mesh/Dispatcher.h>
#include <mesh/Packet.h>
#include <mesh/MeshCore.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <esp_err.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(zephcore_espnow_bridge, CONFIG_ZEPHCORE_MAIN_LOG_LEVEL);

namespace {

constexpr uint32_t BRIDGE_MAGIC = 0x5A434252; /* ZCBR */
constexpr uint8_t BRIDGE_VERSION = 1;
constexpr size_t BRIDGE_RAW_MAX = 236;
constexpr size_t SEEN_SLOTS = 24;
constexpr int64_t SEEN_WINDOW_MS = 120000;
constexpr uint8_t CONTROL_MAGIC[] = { 'Z', 'C', 'B', 'P' };
constexpr uint8_t CONTROL_PING = 1;
constexpr uint8_t CONTROL_PONG = 2;
constexpr uint8_t CONTROL_OBSERVED = 3;
constexpr size_t CONTROL_DATA_LEN = 8;
constexpr size_t CONTROL_LEN = sizeof(CONTROL_MAGIC) + 1 + CONTROL_DATA_LEN;
constexpr uint32_t PING_TIMEOUT_MS = 1500;

struct __packed BridgeFrame {
    uint32_t magic;
    uint32_t hash;
    uint8_t version;
    uint8_t raw_len;
    uint8_t raw[BRIDGE_RAW_MAX];
};
static_assert(sizeof(BridgeFrame) <= ESP_NOW_MAX_DATA_LEN, "ESP-NOW frame too large");

struct SeenFrame {
    uint32_t hash;
    int64_t at_ms;
};

K_MSGQ_DEFINE(espnow_bridge_rx_queue, sizeof(BridgeFrame), 4, 4);

static RepeaterDataStore *s_store;
static mesh::Dispatcher *s_dispatcher;
static RepeaterBridgePrefs s_prefs;
static uint8_t s_local_mac[ESP_NOW_ETH_ALEN];
static uint8_t s_added_peer[ESP_NOW_ETH_ALEN];
static SeenFrame s_seen[SEEN_SLOTS];
static struct k_spinlock s_seen_lock;
static struct k_spinlock s_prefs_lock;
static bool s_started;
static bool s_peer_added;
static atomic_t s_tx_count;
static atomic_t s_rx_count;
static atomic_t s_drop_count;
static atomic_t s_tx_delivered_count;
static atomic_t s_tx_failed_count;
static atomic_t s_ping_sequence;
static atomic_t s_ping_token;
static atomic_t s_ping_sent_ms;
static atomic_t s_ping_rtt_ms;
K_SEM_DEFINE(espnow_bridge_ping_sem, 0, 1);

/* Both images have this bootstrap key so a new pair can exchange packets as
 * soon as their MACs are set. `set bridge.key` replaces it on each unit. */
static const uint8_t s_default_lmk[ESP_NOW_KEY_LEN] = {
    0x5A, 0x43, 0x42, 0x52, 0x49, 0x44, 0x47, 0x45,
    0x2D, 0x31, 0x2E, 0x31, 0x36, 0x2E, 0x38, 0x21,
};
static const uint8_t s_pmk[ESP_NOW_KEY_LEN] = {
    0x5A, 0x43, 0x2D, 0x45, 0x53, 0x50, 0x4E, 0x4F,
    0x57, 0x2D, 0x50, 0x4D, 0x4B, 0x2D, 0x30, 0x31,
};

static bool mac_is_set(const uint8_t mac[ESP_NOW_ETH_ALEN])
{
    bool all_zero = true;
    bool all_ff = true;
    for (size_t i = 0; i < ESP_NOW_ETH_ALEN; ++i) {
        all_zero &= mac[i] == 0;
        all_ff &= mac[i] == 0xFF;
    }
    return !all_zero && !all_ff;
}

static void format_mac(char *out, size_t out_len, const uint8_t mac[ESP_NOW_ETH_ALEN])
{
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
}

static bool parse_mac(const char *text, uint8_t mac[ESP_NOW_ETH_ALEN])
{
    unsigned int bytes[ESP_NOW_ETH_ALEN];
    char tail;
    if (sscanf(text, "%02x:%02x:%02x:%02x:%02x:%02x%c", &bytes[0], &bytes[1], &bytes[2],
               &bytes[3], &bytes[4], &bytes[5], &tail) != ESP_NOW_ETH_ALEN) {
        return false;
    }
    for (size_t i = 0; i < ESP_NOW_ETH_ALEN; ++i) mac[i] = (uint8_t)bytes[i];
    return mac_is_set(mac);
}

static bool parse_key(const char *text, uint8_t key[ESP_NOW_KEY_LEN])
{
    if (strlen(text) != ESP_NOW_KEY_LEN * 2) return false;
    for (size_t i = 0; i < ESP_NOW_KEY_LEN; ++i) {
        unsigned int value;
        if (sscanf(&text[i * 2], "%02x", &value) != 1) return false;
        key[i] = (uint8_t)value;
    }
    return true;
}

static uint32_t hash_raw(const uint8_t *raw, size_t len)
{
    uint32_t hash = 2166136261u;
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
    for (size_t i = 0; i < SEEN_SLOTS; ++i) {
        if (s_seen[i].hash == hash && now - s_seen[i].at_ms < SEEN_WINDOW_MS) {
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

static void send_callback(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
	ARG_UNUSED(info);
	if (status == ESP_NOW_SEND_SUCCESS) {
		atomic_inc(&s_tx_delivered_count);
	} else {
		atomic_inc(&s_tx_failed_count);
	}
}

static bool send_control(uint8_t op, const uint8_t data[CONTROL_DATA_LEN]);
static bool handle_control(const BridgeFrame &frame);

static void receive_callback(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || !data || len < (int)offsetof(BridgeFrame, raw)) return;
    uint8_t peer_mac[ESP_NOW_ETH_ALEN];
    k_spinlock_key_t key = k_spin_lock(&s_prefs_lock);
    memcpy(peer_mac, s_prefs.peer_mac, sizeof(peer_mac));
    k_spin_unlock(&s_prefs_lock, key);
    if (memcmp(info->src_addr, peer_mac, sizeof(peer_mac)) != 0) return;

    BridgeFrame frame{};
    memcpy(&frame, data, len > (int)sizeof(frame) ? sizeof(frame) : (size_t)len);
    if (frame.magic != BRIDGE_MAGIC || frame.version != BRIDGE_VERSION ||
        frame.raw_len < 2 || frame.raw_len > BRIDGE_RAW_MAX ||
        len != (int)(offsetof(BridgeFrame, raw) + frame.raw_len) ||
        frame.hash != hash_raw(frame.raw, frame.raw_len)) {
        atomic_inc(&s_drop_count);
        return;
    }
    if (handle_control(frame)) {
        atomic_inc(&s_rx_count);
        return;
    }
    if (seen_or_remember(frame.hash)) return;
    if (k_msgq_put(&espnow_bridge_rx_queue, &frame, K_NO_WAIT) != 0) {
        atomic_inc(&s_drop_count);
        return;
    }
    atomic_inc(&s_rx_count);
    if (s_dispatcher) s_dispatcher->notifyWake();
}

static bool configure_peer()
{
    if (!s_started || !mac_is_set(s_prefs.peer_mac)) return false;
    if (s_peer_added) {
        esp_now_del_peer(s_added_peer);
        s_peer_added = false;
    }

    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, s_prefs.peer_mac, ESP_NOW_ETH_ALEN);
    memcpy(peer.lmk, s_prefs.lmk, ESP_NOW_KEY_LEN);
    peer.channel = 1;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = true;
    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        LOG_ERR("ESP-NOW add peer failed: %d", err);
        return false;
    }
    s_peer_added = true;
    memcpy(s_added_peer, s_prefs.peer_mac, sizeof(s_added_peer));
    return true;
}

static bool apply_changed_prefs()
{
    if (!s_store || !s_store->saveBridgePrefs(s_prefs)) return false;
    return !s_started || !mac_is_set(s_prefs.peer_mac) || configure_peer();
}

static bool send_control(uint8_t op, const uint8_t data[CONTROL_DATA_LEN])
{
    uint8_t peer_mac[ESP_NOW_ETH_ALEN];
    BridgeFrame frame{};

    if (!s_peer_added) return false;
    k_spinlock_key_t key = k_spin_lock(&s_prefs_lock);
    memcpy(peer_mac, s_prefs.peer_mac, sizeof(peer_mac));
    k_spin_unlock(&s_prefs_lock, key);
    frame.magic = BRIDGE_MAGIC;
    frame.version = BRIDGE_VERSION;
    frame.raw_len = CONTROL_LEN;
    memcpy(frame.raw, CONTROL_MAGIC, sizeof(CONTROL_MAGIC));
    frame.raw[sizeof(CONTROL_MAGIC)] = op;
    memcpy(&frame.raw[sizeof(CONTROL_MAGIC) + 1], data, CONTROL_DATA_LEN);
    frame.hash = hash_raw(frame.raw, frame.raw_len);
    esp_err_t err = esp_now_send(peer_mac, reinterpret_cast<const uint8_t *>(&frame),
                                 offsetof(BridgeFrame, raw) + frame.raw_len);
    if (err != ESP_OK) {
        LOG_WRN("ESP-NOW control send failed: %d", err);
        return false;
    }
    atomic_inc(&s_tx_count);
    return true;
}

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
        k_sem_give(&espnow_bridge_ping_sem);
    } else if (frame.raw[sizeof(CONTROL_MAGIC)] == CONTROL_OBSERVED) {
        repeater_bridge_peer_observed(data);
    }
    return true;
}

} // namespace

bool espnow_bridge_start(RepeaterDataStore *store, mesh::Dispatcher *dispatcher)
{
    s_store = store;
    s_dispatcher = dispatcher;
    memset(&s_prefs, 0, sizeof(s_prefs));
    if (!s_store->loadBridgePrefs(s_prefs)) {
        memcpy(s_prefs.lmk, s_default_lmk, sizeof(s_prefs.lmk));
        s_prefs.key_is_custom = 0;
        s_store->saveBridgePrefs(s_prefs);
    }

    esp_err_t err = esp_wifi_set_mode(ESP32_WIFI_MODE_STA);
    if (err != ESP_OK) {
        LOG_ERR("ESP-NOW WiFi mode failed: %d", err);
        return false;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        LOG_ERR("ESP-NOW WiFi start failed: %d", err);
        return false;
    }
    err = esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        LOG_ERR("ESP-NOW channel failed: %d", err);
        return false;
    }
    err = esp_wifi_get_mac(WIFI_IF_STA, s_local_mac);
    if (err != ESP_OK) {
        LOG_ERR("ESP-NOW get MAC failed: %d", err);
        return false;
    }
    err = esp_now_init();
    if (err != ESP_OK) {
        LOG_ERR("ESP-NOW init failed: %d", err);
        return false;
    }
    if (esp_now_set_pmk(s_pmk) != ESP_OK || esp_now_register_recv_cb(receive_callback) != ESP_OK ||
        esp_now_register_send_cb(send_callback) != ESP_OK) {
        LOG_ERR("ESP-NOW callback/key setup failed");
        return false;
    }
    s_started = true;
    if (mac_is_set(s_prefs.peer_mac)) configure_peer();

    char mac[18];
    format_mac(mac, sizeof(mac), s_local_mac);
    LOG_INF("ESP-NOW bridge ready: local %s, peer %s", mac,
            s_peer_added ? "configured" : "not set");
    return true;
}

bool espnow_bridge_get_local_mac(char *out, size_t out_len)
{
	uint8_t mac[ESP_NOW_ETH_ALEN];

	if (!out || out_len < 18 || esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) return false;
	format_mac(out, out_len, mac);
	return true;
}

bool espnow_bridge_ping(char *reply, size_t reply_len)
{
	if (!reply || reply_len == 0) return false;
	if (!s_started || !s_peer_added) {
		snprintf(reply, reply_len, "ERR: bridge peer is not connected");
		return true;
	}
	while (k_sem_take(&espnow_bridge_ping_sem, K_NO_WAIT) == 0) {}
	uint32_t token = (uint32_t)atomic_inc(&s_ping_sequence) + 1;
	if (token == 0) token = (uint32_t)atomic_inc(&s_ping_sequence) + 1;
	atomic_set(&s_ping_token, token);
	atomic_set(&s_ping_sent_ms, (uint32_t)k_uptime_get());
	uint8_t ping_data[CONTROL_DATA_LEN] = {};
	memcpy(ping_data, &token, sizeof(token));
	if (!send_control(CONTROL_PING, ping_data)) {
		atomic_set(&s_ping_token, 0);
		snprintf(reply, reply_len, "ERR: bridge ping send failed");
		return true;
	}
	if (k_sem_take(&espnow_bridge_ping_sem, K_MSEC(PING_TIMEOUT_MS)) == 0) {
		snprintf(reply, reply_len, "OK: bridge pong %ums",
			 (unsigned int)atomic_get(&s_ping_rtt_ms));
	} else {
		atomic_set(&s_ping_token, 0);
		snprintf(reply, reply_len, "ERR: bridge ping timeout");
	}
	return true;
}

bool espnow_bridge_handle_command(const char *command, char *reply, size_t reply_len)
{
    if (!command || !reply || reply_len == 0) return false;
    if (strcmp(command, "get bridge.mac") == 0 || strcmp(command, "get bridge.peer") == 0) {
        char local[18] = "unavailable";
        char peer[18] = "not set";
        if (s_started) format_mac(local, sizeof(local), s_local_mac);
        if (mac_is_set(s_prefs.peer_mac)) format_mac(peer, sizeof(peer), s_prefs.peer_mac);
        snprintf(reply, reply_len,
                 "bridge: %s; transport=ESP-NOW; local=%s; peer=%s; channel=1; encrypted; key=%s; tx=%u ok=%u fail=%u rx=%u drop=%u",
                 s_peer_added ? "configured" : (s_started ? "peer not set" : "offline"), local, peer,
                 s_prefs.key_is_custom ? "custom" : "default", (unsigned int)atomic_get(&s_tx_count),
                 (unsigned int)atomic_get(&s_tx_delivered_count),
                 (unsigned int)atomic_get(&s_tx_failed_count),
                 (unsigned int)atomic_get(&s_rx_count), (unsigned int)atomic_get(&s_drop_count));
        return true;
    }

    const char *peer_value = nullptr;
    if (strncmp(command, "set bridge ", 11) == 0) peer_value = command + 11;
    if (strncmp(command, "set bridge.peer ", 16) == 0) peer_value = command + 16;
    if (peer_value) {
        uint8_t peer[ESP_NOW_ETH_ALEN];
        if (!parse_mac(peer_value, peer)) {
            snprintf(reply, reply_len, "ERR: use set bridge.peer AA:BB:CC:DD:EE:FF");
            return true;
        }
        k_spinlock_key_t lock_key = k_spin_lock(&s_prefs_lock);
        memcpy(s_prefs.peer_mac, peer, sizeof(peer));
        k_spin_unlock(&s_prefs_lock, lock_key);
        if (!apply_changed_prefs()) {
            snprintf(reply, reply_len, "ERR: bridge peer not saved");
            return true;
        }
        snprintf(reply, reply_len, "OK: bridge peer saved; waiting for first delivery");
        return true;
    }

    if (strncmp(command, "set bridge.key ", 15) == 0) {
        uint8_t key[ESP_NOW_KEY_LEN];
        if (!parse_key(command + 15, key)) {
            snprintf(reply, reply_len, "ERR: bridge.key needs 32 hex characters");
            return true;
        }
        k_spinlock_key_t lock_key = k_spin_lock(&s_prefs_lock);
        memcpy(s_prefs.lmk, key, sizeof(key));
        k_spin_unlock(&s_prefs_lock, lock_key);
        s_prefs.key_is_custom = 1;
        if (!apply_changed_prefs()) {
            snprintf(reply, reply_len, "ERR: bridge key not saved");
            return true;
        }
        snprintf(reply, reply_len, "OK: bridge key saved; set the same key on peer");
        return true;
    }
    return false;
}

bool espnow_bridge_forward_packet(const mesh::Packet *packet)
{
    if (!s_peer_added || !packet) return false;
    const int raw_len = packet->getRawLength();
    if (raw_len < 2 || raw_len > (int)BRIDGE_RAW_MAX) {
        if (raw_len > (int)BRIDGE_RAW_MAX) {
            atomic_inc(&s_drop_count);
            LOG_WRN("ESP-NOW bridge skips %d-byte packet", raw_len);
        }
        return false;
    }
    BridgeFrame frame{};
    frame.magic = BRIDGE_MAGIC;
    frame.version = BRIDGE_VERSION;
    frame.raw_len = raw_len;
    packet->writeTo(frame.raw);
    frame.hash = hash_raw(frame.raw, frame.raw_len);
    if (seen_or_remember(frame.hash)) return false;
    esp_err_t err = esp_now_send(s_prefs.peer_mac, reinterpret_cast<const uint8_t *>(&frame),
                                 offsetof(BridgeFrame, raw) + frame.raw_len);
    if (err != ESP_OK) {
        LOG_WRN("ESP-NOW send failed: %d", err);
        return false;
    }
    atomic_inc(&s_tx_count);
    return true;
}

bool espnow_bridge_send_observed(const uint8_t fingerprint[8])
{
    return fingerprint && send_control(CONTROL_OBSERVED, fingerprint);
}

void espnow_bridge_drain(mesh::Dispatcher *dispatcher)
{
    BridgeFrame frame;
    while (k_msgq_get(&espnow_bridge_rx_queue, &frame, K_NO_WAIT) == 0) {
        repeater_bridge_note_inbound_raw(frame.raw, frame.raw_len);
        dispatcher->injectRaw(frame.raw, frame.raw_len);
    }
}

void espnow_bridge_stop()
{
    if (!s_started) return;
    if (s_peer_added) esp_now_del_peer(s_added_peer);
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
    esp_wifi_stop();
    s_peer_added = false;
    s_started = false;
    s_dispatcher = nullptr;
    k_msgq_purge(&espnow_bridge_rx_queue);
}

bool espnow_bridge_is_connected(void)
{
    /* ESP-NOW is connectionless: a configured peer is the active link. */
    return s_started && s_peer_added;
}
