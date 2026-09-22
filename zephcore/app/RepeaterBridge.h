/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdint.h>
#include <stddef.h>

class RepeaterDataStore;

namespace mesh {
class Dispatcher;
class Packet;
}

/* Platform transport: ESP-NOW on ESP32, encrypted BLE GATT on nRF. */
bool repeater_bridge_start(RepeaterDataStore *store, mesh::Dispatcher *dispatcher);
bool repeater_bridge_is_enabled(void);
bool repeater_bridge_is_connected(void);
const char *repeater_bridge_status(void);
void repeater_bridge_get_addresses(char *local, size_t local_len, char *peer, size_t peer_len);
void repeater_bridge_get_metrics(uint8_t *priority, uint32_t *forwarded, uint32_t *skipped);
bool repeater_bridge_handle_command(const char *command, char *reply, size_t reply_len);
bool repeater_bridge_forward_packet(const mesh::Packet *packet, uint32_t local_airtime_ms);
void repeater_bridge_note_inbound_raw(const uint8_t *raw, size_t raw_len);
void repeater_bridge_peer_observed(const uint8_t fingerprint[8]);
void repeater_bridge_drain(mesh::Dispatcher *dispatcher);
uint32_t repeater_bridge_ms_until_next(void);
