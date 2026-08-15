/* SPDX-License-Identifier: MIT */

#include "RepeaterBridge.h"
#include "BridgeTransport.h"
#include "RepeaterDataStore.h"

#include <mesh/MeshCore.h>
#include <mesh/Packet.h>

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/atomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

enum BridgeTransport : uint8_t {
	BRIDGE_BLE = 0,
	BRIDGE_ESPNOW = 1,
};

static RepeaterDataStore *s_store;
static mesh::Dispatcher *s_dispatcher;
static BridgeTransport s_transport;
static bool s_enabled;

enum BridgeDiagnostic : uint8_t {
	BRIDGE_DIAG_NONE,
	BRIDGE_DIAG_TIMEOUT,
	BRIDGE_DIAG_ERROR,
};

static BridgeDiagnostic s_diagnostic;

/* A bridge receives packet variants whose LoRa path has changed after a
 * repeater retransmit. Packet::calculatePacketHash deliberately excludes that
 * mutable path, so this cache identifies the logical message rather than one
 * radio copy and suppresses repeat bridge crossings within its window. */
constexpr size_t FINGERPRINT_SLOTS = 128;
constexpr int64_t FINGERPRINT_WINDOW_MS = 10 * 60 * 1000;
constexpr size_t PENDING_SLOTS = 4;
constexpr uint32_t FORWARD_SLOT_MIN_MS = 5000;
constexpr uint32_t FORWARD_SLOT_MAX_MS = 30000;
constexpr uint32_t FORWARD_SLOT_GUARD_MS = 1000;
constexpr uint8_t FORWARD_PRIORITY_MAX = 7;

static uint8_t s_forward_priority = FORWARD_PRIORITY_MAX;
static atomic_t s_forwarded_count;
static atomic_t s_skipped_count;

struct FingerprintEntry {
	uint8_t bytes[MAX_HASH_SIZE];
	int64_t at_ms;
};

struct PendingForward {
	uint8_t fingerprint[MAX_HASH_SIZE];
	uint8_t raw[MAX_TRANS_UNIT];
	uint8_t raw_len;
	int64_t due_ms;
	bool used;
};

static FingerprintEntry s_fingerprints[FINGERPRINT_SLOTS];
static PendingForward s_pending[PENDING_SLOTS];
static struct k_spinlock s_fingerprint_lock;
static struct k_spinlock s_pending_lock;

static void clear_pending()
{
	k_spinlock_key_t key = k_spin_lock(&s_pending_lock);
	memset(s_pending, 0, sizeof(s_pending));
	k_spin_unlock(&s_pending_lock, key);
}

static bool packet_is_bridgeable(const mesh::Packet &packet)
{
	if (!packet.isRouteFlood()) return false;
	const uint8_t type = packet.getPayloadType();
	return type == PAYLOAD_TYPE_GRP_TXT || type == PAYLOAD_TYPE_GRP_DATA ||
	       type == PAYLOAD_TYPE_ADVERT;
}

static void packet_fingerprint(const mesh::Packet &packet, uint8_t out[MAX_HASH_SIZE])
{
	packet.calculatePacketHash(out);
}

static bool fingerprint_seen_or_remember(const uint8_t fingerprint[MAX_HASH_SIZE])
{
	const int64_t now = k_uptime_get();
	k_spinlock_key_t key = k_spin_lock(&s_fingerprint_lock);
	for (const auto &entry : s_fingerprints) {
		if (entry.at_ms != 0 && now - entry.at_ms < FINGERPRINT_WINDOW_MS &&
		    memcmp(entry.bytes, fingerprint, MAX_HASH_SIZE) == 0) {
			k_spin_unlock(&s_fingerprint_lock, key);
			return true;
		}
	}
	size_t oldest = 0;
	for (size_t i = 1; i < FINGERPRINT_SLOTS; ++i) {
		if (s_fingerprints[i].at_ms < s_fingerprints[oldest].at_ms) oldest = i;
	}
	memcpy(s_fingerprints[oldest].bytes, fingerprint, MAX_HASH_SIZE);
	s_fingerprints[oldest].at_ms = now == 0 ? 1 : now;
	k_spin_unlock(&s_fingerprint_lock, key);
	return false;
}

static uint32_t forward_slot_ms(uint32_t airtime_ms)
{
	/* A normal flood relay may wait up to its 2 s jitter cap, then occupy one
	 * packet airtime. This envelope leaves enough time for peer observation. */
	uint64_t slot = (uint64_t)airtime_ms * 4U + FORWARD_SLOT_GUARD_MS;
	if (slot < FORWARD_SLOT_MIN_MS) return FORWARD_SLOT_MIN_MS;
	if (slot > FORWARD_SLOT_MAX_MS) return FORWARD_SLOT_MAX_MS;
	return (uint32_t)slot;
}

static bool send_observed(const uint8_t fingerprint[MAX_HASH_SIZE])
{
	if (!s_enabled) return false;
	if (s_transport == BRIDGE_BLE) return ble_bridge_send_observed(fingerprint);
#if defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
	return espnow_bridge_send_observed(fingerprint);
#else
	return false;
#endif
}

static const uint8_t s_default_lmk[16] = {
	0x5A, 0x43, 0x42, 0x52, 0x49, 0x44, 0x47, 0x45,
	0x2D, 0x31, 0x2E, 0x31, 0x36, 0x2E, 0x38, 0x21,
};

static BridgeTransport default_transport()
{
#if defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
	return BRIDGE_ESPNOW;
#else
	return BRIDGE_BLE;
#endif
}

static bool transport_available(BridgeTransport transport)
{
#if !defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
	return transport == BRIDGE_BLE;
#else
	return transport == BRIDGE_BLE || transport == BRIDGE_ESPNOW;
#endif
}

static const char *transport_name(BridgeTransport transport)
{
	return transport == BRIDGE_ESPNOW ? "esp-now" : "ble";
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

static bool parse_peer(const char *text, uint8_t mac[6], uint8_t *addr_type)
{
	unsigned int bytes[6];
	char type[8] = {};
	if (sscanf(text, "%02x:%02x:%02x:%02x:%02x:%02x %7s", &bytes[0], &bytes[1], &bytes[2],
		   &bytes[3], &bytes[4], &bytes[5], type) < 6) return false;
	if (type[0] == '\0' || strcmp(type, "public") == 0) {
		*addr_type = 0;
	} else if (strcmp(type, "random") == 0) {
		*addr_type = 1;
	} else {
		return false;
	}
	bool all_zero = true;
	bool all_ff = true;
	for (size_t i = 0; i < 6; ++i) {
		mac[i] = (uint8_t)bytes[i];
		all_zero &= mac[i] == 0;
		all_ff &= mac[i] == 0xFF;
	}
	return !all_zero && !all_ff;
}

static void format_mac(char *out, size_t out_len, const uint8_t mac[6])
{
	snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
		 mac[3], mac[4], mac[5]);
}

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

static bool start_active()
{
	if (s_transport == BRIDGE_BLE) return ble_bridge_start(s_store, s_dispatcher);
#if defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
	return espnow_bridge_start(s_store, s_dispatcher);
#else
	return false;
#endif
}

static void stop_active()
{
	if (s_transport == BRIDGE_BLE) {
		ble_bridge_stop();
	} else {
#if defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
		espnow_bridge_stop();
#endif
	}
	clear_pending();
}

} // namespace

bool repeater_bridge_start(RepeaterDataStore *store, mesh::Dispatcher *dispatcher)
{
	s_store = store;
	s_dispatcher = dispatcher;
	RepeaterBridgePrefs prefs{};
	if (!s_store->loadBridgePrefs(prefs)) {
		memcpy(prefs.lmk, s_default_lmk, sizeof(prefs.lmk));
		prefs.transport = default_transport();
		prefs.enabled = 0;
		prefs.forward_priority = FORWARD_PRIORITY_MAX;
		s_store->saveBridgePrefs(prefs);
	}
	if (prefs.transport > BRIDGE_ESPNOW || !transport_available((BridgeTransport)prefs.transport)) {
		prefs.transport = default_transport();
		s_store->saveBridgePrefs(prefs);
	}
	s_transport = (BridgeTransport)prefs.transport;
	if (prefs.forward_priority > FORWARD_PRIORITY_MAX) {
		prefs.forward_priority = FORWARD_PRIORITY_MAX;
		s_store->saveBridgePrefs(prefs);
	}
	s_forward_priority = prefs.forward_priority;
	s_enabled = prefs.enabled != 0;
	clear_pending();
	return !s_enabled || start_active();
}

bool repeater_bridge_is_enabled(void)
{
	return s_enabled;
}

bool repeater_bridge_is_connected(void)
{
	if (!s_enabled) return false;
	if (s_transport == BRIDGE_BLE) return ble_bridge_is_connected();
#if defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
	return espnow_bridge_is_connected();
#else
	return false;
#endif
}

const char *repeater_bridge_status(void)
{
	RepeaterBridgePrefs prefs{};

	if (!s_enabled) return s_diagnostic == BRIDGE_DIAG_ERROR ? "error" : "off";
	if (!s_store || !s_store->loadBridgePrefs(prefs) || !mac_is_set(prefs.peer_mac)) {
		return "no peer";
	}
	if (s_diagnostic == BRIDGE_DIAG_TIMEOUT) return "timeout";
	if (s_diagnostic == BRIDGE_DIAG_ERROR) return "error";
	if (repeater_bridge_is_connected()) return "connected";
	return s_transport == BRIDGE_BLE ? "waiting" : "error";
}

void repeater_bridge_get_addresses(char *local, size_t local_len, char *peer, size_t peer_len)
{
	RepeaterBridgePrefs prefs{};

	if (local && local_len) snprintf(local, local_len, "unavailable");
	if (peer && peer_len) snprintf(peer, peer_len, "not set");
	if (s_store && s_store->loadBridgePrefs(prefs) && mac_is_set(prefs.peer_mac) && peer && peer_len) {
		format_mac(peer, peer_len, prefs.peer_mac);
	}
	if (!local || local_len == 0) return;
	if (s_transport == BRIDGE_BLE) {
		ble_bridge_get_local_mac(local, local_len);
		return;
	}
#if defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
	espnow_bridge_get_local_mac(local, local_len);
#endif
}

void repeater_bridge_get_metrics(uint8_t *priority, uint32_t *forwarded, uint32_t *skipped)
{
	if (priority) *priority = s_forward_priority;
	if (forwarded) *forwarded = (uint32_t)atomic_get(&s_forwarded_count);
	if (skipped) *skipped = (uint32_t)atomic_get(&s_skipped_count);
}

bool repeater_bridge_handle_command(const char *command, char *reply, size_t reply_len)
{
	if (!command || !reply || reply_len == 0) return false;
	if (strcmp(command, "bridge ping") == 0) {
		if (!s_enabled) {
			snprintf(reply, reply_len, "ERR: bridge is off");
			return true;
		}
		bool handled;

		if (s_transport == BRIDGE_BLE) handled = ble_bridge_ping(reply, reply_len);
#if defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
		else handled = espnow_bridge_ping(reply, reply_len);
#else
		else handled = false;
#endif
		if (handled && strncmp(reply, "OK: bridge pong", 15) == 0) {
			s_diagnostic = BRIDGE_DIAG_NONE;
		} else if (handled && strstr(reply, "timeout") != nullptr) {
			s_diagnostic = BRIDGE_DIAG_TIMEOUT;
		} else if (handled && strstr(reply, "send failed") != nullptr) {
			s_diagnostic = BRIDGE_DIAG_ERROR;
		}
		return handled;
	}
	if (strcmp(command, "get bridge.type") == 0) {
		snprintf(reply, reply_len, "bridge.type=%s", transport_name(s_transport));
		return true;
	}
	if (strcmp(command, "get bridge.priority") == 0) {
		RepeaterBridgePrefs prefs{};
		if (!s_store || !s_store->loadBridgePrefs(prefs)) {
			snprintf(reply, reply_len, "ERR: bridge settings unavailable");
		} else {
			snprintf(reply, reply_len, "bridge.priority=%u", prefs.forward_priority);
		}
		return true;
	}
	if (strcmp(command, "get bridge.peer") == 0) {
		RepeaterBridgePrefs prefs{};
		if (!s_store || !s_store->loadBridgePrefs(prefs) || !mac_is_set(prefs.peer_mac)) {
			snprintf(reply, reply_len, "bridge.peer=not set");
		} else {
			char peer[18];
			format_mac(peer, sizeof(peer), prefs.peer_mac);
			snprintf(reply, reply_len, "bridge.peer=%s %s", peer,
				 prefs.peer_addr_type == 1 ? "random" : "public");
		}
		return true;
	}
	if (strcmp(command, "get bridge.delay") == 0) {
		RepeaterBridgePrefs prefs{};
		if (!s_store || !s_store->loadBridgePrefs(prefs)) {
			snprintf(reply, reply_len, "ERR: bridge settings unavailable");
		} else if (prefs.forward_priority == 0) {
			snprintf(reply, reply_len, "Delay: immediate (priority 0)");
		} else {
			snprintf(reply, reply_len, "Delay: %u-%us (priority %u)",
				 (unsigned int)prefs.forward_priority * 5U,
				 (unsigned int)prefs.forward_priority * 30U,
				 (unsigned int)prefs.forward_priority);
		}
		return true;
	}
	if (strncmp(command, "set bridge.priority ", 20) == 0) {
		const char *value = command + 20;
		char *end = nullptr;
		unsigned long priority = strtoul(value, &end, 10);
		if (end == value || *end != '\0' || priority > FORWARD_PRIORITY_MAX) {
			snprintf(reply, reply_len, "ERR: bridge.priority is 0..%u", FORWARD_PRIORITY_MAX);
			return true;
		}
		RepeaterBridgePrefs prefs{};
		if (!s_store || !s_store->loadBridgePrefs(prefs)) {
			snprintf(reply, reply_len, "ERR: bridge settings unavailable");
			return true;
		}
		prefs.forward_priority = (uint8_t)priority;
		if (!s_store->saveBridgePrefs(prefs)) {
			snprintf(reply, reply_len, "ERR: bridge.priority not saved");
			return true;
		}
		s_forward_priority = prefs.forward_priority;
		snprintf(reply, reply_len, "OK: bridge.priority=%u", prefs.forward_priority);
		return true;
	}
	if (strcmp(command, "bridge") == 0) {
		char local[18];
		char peer[18];

		repeater_bridge_get_addresses(local, sizeof(local), peer, sizeof(peer));
		RepeaterBridgePrefs prefs{};
		const unsigned int priority = s_store && s_store->loadBridgePrefs(prefs) ?
			prefs.forward_priority : FORWARD_PRIORITY_MAX;
		snprintf(reply, reply_len, "bridge: %s; type=%s; priority=%u; local=%s; peer=%s",
			 repeater_bridge_status(), transport_name(s_transport), priority, local, peer);
		return true;
	}
	if (strcmp(command, "bridge on") == 0 || strcmp(command, "bridge off") == 0) {
		const bool enabled = strcmp(command, "bridge on") == 0;
		RepeaterBridgePrefs prefs{};
		if (!s_store || !s_store->loadBridgePrefs(prefs)) {
			snprintf(reply, reply_len, "ERR: bridge settings unavailable");
			return true;
		}
		if (enabled && !s_enabled) {
			s_diagnostic = BRIDGE_DIAG_NONE;
			s_enabled = true;
			if (!start_active()) {
				s_enabled = false;
				s_diagnostic = BRIDGE_DIAG_ERROR;
				snprintf(reply, reply_len, "ERR: bridge did not start");
				return true;
			}
		} else if (!enabled && s_enabled) {
			stop_active();
			s_enabled = false;
			s_diagnostic = BRIDGE_DIAG_NONE;
		}
		prefs.enabled = s_enabled ? 1 : 0;
		if (!s_store->saveBridgePrefs(prefs)) {
			if (s_enabled) {
				stop_active();
				s_enabled = false;
			}
			s_diagnostic = BRIDGE_DIAG_ERROR;
			snprintf(reply, reply_len, "ERR: bridge state not saved");
			return true;
		}
		if (!s_enabled) {
			char local[18] = "unavailable";
			char peer[18] = "not set";

			if (mac_is_set(prefs.peer_mac)) format_mac(peer, sizeof(peer), prefs.peer_mac);
#if defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
			espnow_bridge_get_local_mac(local, sizeof(local));
#endif
			snprintf(reply, reply_len, "OK: bridge off; local=%s; peer=%s", local, peer);
		} else {
			snprintf(reply, reply_len, "OK: bridge on");
		}
		return true;
	}
	if (strcmp(command, "bridge keygen") == 0) {
		RepeaterBridgePrefs prefs{};
		if (!s_store || !s_store->loadBridgePrefs(prefs)) {
			snprintf(reply, reply_len, "ERR: bridge settings unavailable");
			return true;
		}
		sys_rand_get(prefs.lmk, sizeof(prefs.lmk));
		prefs.key_is_custom = 1;
		if (!s_store->saveBridgePrefs(prefs)) {
			snprintf(reply, reply_len, "ERR: bridge key not saved");
			return true;
		}
		if (s_enabled) {
			stop_active();
			if (!start_active()) {
				s_enabled = false;
				s_diagnostic = BRIDGE_DIAG_ERROR;
				prefs.enabled = 0;
				s_store->saveBridgePrefs(prefs);
				snprintf(reply, reply_len, "ERR: bridge key saved; bridge disabled after restart failure");
				return true;
			}
		}
		char hex[sizeof(prefs.lmk) * 2 + 1];
		for (size_t i = 0; i < sizeof(prefs.lmk); ++i) {
			snprintf(&hex[i * 2], 3, "%02X", prefs.lmk[i]);
		}
		snprintf(reply, reply_len, "OK: bridge.key=%s; set same key on peer", hex);
		return true;
	}
	if (strncmp(command, "set bridge.type ", 16) == 0) {
		const char *value = command + 16;
		BridgeTransport next;
		if (strcmp(value, "ble") == 0) next = BRIDGE_BLE;
		else if (strcmp(value, "esp-now") == 0) next = BRIDGE_ESPNOW;
		else {
			snprintf(reply, reply_len, "ERR: bridge.type is ble or esp-now");
			return true;
		}
		if (!transport_available(next)) {
			snprintf(reply, reply_len, "ERR: esp-now is available only on ESP32");
			return true;
		}
		RepeaterBridgePrefs prefs{};
		if (!s_store || !s_store->loadBridgePrefs(prefs)) {
			snprintf(reply, reply_len, "ERR: bridge settings unavailable");
			return true;
		}
		const BridgeTransport old = s_transport;
		if (next != s_transport) {
			if (s_enabled) stop_active();
			s_transport = next;
			if (s_enabled && !start_active()) {
				s_transport = old;
				start_active();
				s_diagnostic = BRIDGE_DIAG_ERROR;
				snprintf(reply, reply_len, "ERR: bridge %s did not start", transport_name(next));
				return true;
			}
		}
		prefs.transport = next;
		if (!s_store->saveBridgePrefs(prefs)) {
			if (next != old && s_enabled) {
				stop_active();
				s_transport = old;
				start_active();
			} else {
				s_transport = old;
			}
			s_diagnostic = BRIDGE_DIAG_ERROR;
			snprintf(reply, reply_len, "ERR: bridge.type not saved");
			return true;
		}
		snprintf(reply, reply_len, "OK: bridge.type=%s", transport_name(s_transport));
		return true;
	}
	/* Keep configuration possible while the radio backhaul is off. The
	 * transport objects are deliberately not started merely to write flash. */
	if (!s_enabled && strncmp(command, "set bridge.peer ", 16) == 0) {
		RepeaterBridgePrefs prefs{};
		uint8_t peer[6];
		uint8_t peer_addr_type;
		if (!parse_peer(command + 16, peer, &peer_addr_type)) {
			snprintf(reply, reply_len, "ERR: use set bridge.peer MAC [public|random]");
			return true;
		}
		if (!s_store || !s_store->loadBridgePrefs(prefs)) {
			snprintf(reply, reply_len, "ERR: bridge settings unavailable");
			return true;
		}
		memcpy(prefs.peer_mac, peer, sizeof(peer));
		prefs.peer_addr_type = peer_addr_type;
		const bool saved = s_store->saveBridgePrefs(prefs);
		s_diagnostic = saved ? BRIDGE_DIAG_NONE : BRIDGE_DIAG_ERROR;
		snprintf(reply, reply_len, saved ? "OK: bridge peer saved" : "ERR: bridge peer not saved");
		return true;
	}
	if (!s_enabled && strncmp(command, "set bridge.key ", 15) == 0) {
		RepeaterBridgePrefs prefs{};
		uint8_t key[16];
		if (!parse_key(command + 15, key)) {
			snprintf(reply, reply_len, "ERR: bridge.key needs 32 hex characters");
			return true;
		}
		if (!s_store || !s_store->loadBridgePrefs(prefs)) {
			snprintf(reply, reply_len, "ERR: bridge settings unavailable");
			return true;
		}
		memcpy(prefs.lmk, key, sizeof(key));
		prefs.key_is_custom = 1;
		const bool saved = s_store->saveBridgePrefs(prefs);
		s_diagnostic = saved ? BRIDGE_DIAG_NONE : BRIDGE_DIAG_ERROR;
		snprintf(reply, reply_len, saved ? "OK: bridge key saved; set same key on peer" :
			 "ERR: bridge key not saved");
		return true;
	}

	bool handled;
	if (s_transport == BRIDGE_BLE) handled = ble_bridge_handle_command(command, reply, reply_len);
#if defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
	else handled = espnow_bridge_handle_command(command, reply, reply_len);
#else
	else handled = false;
#endif
	if (handled && (strncmp(command, "set bridge.peer ", 16) == 0 ||
			strncmp(command, "set bridge.key ", 15) == 0)) {
		s_diagnostic = strncmp(reply, "OK:", 3) == 0 ? BRIDGE_DIAG_NONE : BRIDGE_DIAG_ERROR;
	}
	return handled;
}

void repeater_bridge_note_inbound_raw(const uint8_t *raw, size_t raw_len)
{
	if (!raw || raw_len < 2 || raw_len > MAX_TRANS_UNIT) return;
	mesh::Packet packet;
	if (!packet.readFrom(raw, (uint8_t)raw_len) || !packet_is_bridgeable(packet)) return;
	uint8_t fingerprint[MAX_HASH_SIZE];
	packet_fingerprint(packet, fingerprint);
	(void)fingerprint_seen_or_remember(fingerprint);
}

void repeater_bridge_peer_observed(const uint8_t fingerprint[MAX_HASH_SIZE])
{
	if (!fingerprint) return;
	(void)fingerprint_seen_or_remember(fingerprint);
	bool cancelled = false;
	k_spinlock_key_t key = k_spin_lock(&s_pending_lock);
	for (auto &pending : s_pending) {
		if (pending.used && memcmp(pending.fingerprint, fingerprint, MAX_HASH_SIZE) == 0) {
			pending.used = false;
			cancelled = true;
		}
	}
	k_spin_unlock(&s_pending_lock, key);
	if (cancelled) atomic_inc(&s_skipped_count);
	if (s_dispatcher) s_dispatcher->notifyWake();
}

bool repeater_bridge_forward_packet(const mesh::Packet *packet, uint32_t local_airtime_ms)
{
	if (!s_enabled || !repeater_bridge_is_connected() || !packet || !packet_is_bridgeable(*packet)) {
		return false;
	}
	uint8_t fingerprint[MAX_HASH_SIZE];
	packet_fingerprint(*packet, fingerprint);
	if (fingerprint_seen_or_remember(fingerprint)) return false;

	/* The peer only needs to report that this logical message was already
	 * present on its LoRa side. It never emits a LoRa control packet. */
	(void)send_observed(fingerprint);

	const int raw_len = packet->getRawLength();
	if (raw_len < 2 || raw_len > MAX_TRANS_UNIT) return false;
	RepeaterBridgePrefs prefs{};
	if (!s_store || !s_store->loadBridgePrefs(prefs)) return false;
	const uint32_t slot_ms = forward_slot_ms(local_airtime_ms);
	const int64_t due_ms = k_uptime_get() + (int64_t)prefs.forward_priority * slot_ms;

	k_spinlock_key_t key = k_spin_lock(&s_pending_lock);
	PendingForward *slot = nullptr;
	for (auto &pending : s_pending) {
		if (!pending.used) {
			slot = &pending;
			break;
		}
	}
	if (!slot) {
		k_spin_unlock(&s_pending_lock, key);
		return false;
	}
	memcpy(slot->fingerprint, fingerprint, MAX_HASH_SIZE);
	slot->raw_len = (uint8_t)raw_len;
	packet->writeTo(slot->raw);
	slot->due_ms = due_ms;
	slot->used = true;
	k_spin_unlock(&s_pending_lock, key);
	if (s_dispatcher) s_dispatcher->notifyWake();
	return true;
}

void repeater_bridge_drain(mesh::Dispatcher *dispatcher)
{
	if (!s_enabled) return;
	if (s_transport == BRIDGE_BLE) {
		ble_bridge_drain(dispatcher);
	} else {
#if defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
		espnow_bridge_drain(dispatcher);
#endif
	}

	const int64_t now = k_uptime_get();
	for (size_t i = 0; i < PENDING_SLOTS; ++i) {
		PendingForward pending{};
		k_spinlock_key_t key = k_spin_lock(&s_pending_lock);
		if (!s_pending[i].used || s_pending[i].due_ms > now) {
			k_spin_unlock(&s_pending_lock, key);
			continue;
		}
		pending = s_pending[i];
		s_pending[i].used = false;
		k_spin_unlock(&s_pending_lock, key);

		mesh::Packet packet;
		if (!packet.readFrom(pending.raw, pending.raw_len)) continue;
		bool forwarded = false;
		if (s_transport == BRIDGE_BLE) {
			forwarded = ble_bridge_forward_packet(&packet);
		} else {
#if defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
			forwarded = espnow_bridge_forward_packet(&packet);
#endif
		}
		if (forwarded) atomic_inc(&s_forwarded_count);
	}
}

uint32_t repeater_bridge_ms_until_next(void)
{
	if (!s_enabled) return UINT32_MAX;
	const int64_t now = k_uptime_get();
	uint32_t next = UINT32_MAX;
	k_spinlock_key_t key = k_spin_lock(&s_pending_lock);
	for (const auto &pending : s_pending) {
		if (!pending.used) continue;
		const int64_t remaining = pending.due_ms - now;
		const uint32_t delay = remaining <= 0 ? 0 :
			(remaining > INT32_MAX ? UINT32_MAX : (uint32_t)remaining);
		if (delay < next) next = delay;
	}
	k_spin_unlock(&s_pending_lock, key);
	return next;
}
