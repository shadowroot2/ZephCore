/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdint.h>
#include <stddef.h>

class RepeaterDataStore;

namespace mesh {
class Dispatcher;
class Packet;
}

bool espnow_bridge_start(RepeaterDataStore *store, mesh::Dispatcher *dispatcher);
void espnow_bridge_stop();
bool espnow_bridge_get_local_mac(char *out, size_t out_len);
bool espnow_bridge_is_connected(void);
bool espnow_bridge_ping(char *reply, size_t reply_len);
bool espnow_bridge_handle_command(const char *command, char *reply, size_t reply_len);
bool espnow_bridge_forward_packet(const mesh::Packet *packet);
bool espnow_bridge_send_observed(const uint8_t fingerprint[8]);
void espnow_bridge_drain(mesh::Dispatcher *dispatcher);

bool ble_bridge_start(RepeaterDataStore *store, mesh::Dispatcher *dispatcher);
void ble_bridge_stop();
bool ble_bridge_get_local_mac(char *out, size_t out_len);
void ble_bridge_get_diagnostics(char *out, size_t out_len);
bool ble_bridge_unpair(void);
bool ble_bridge_is_connected(void);
bool ble_bridge_ping(char *reply, size_t reply_len);
bool ble_bridge_handle_command(const char *command, char *reply, size_t reply_len);
bool ble_bridge_forward_packet(const mesh::Packet *packet);
bool ble_bridge_send_observed(const uint8_t fingerprint[8]);
void ble_bridge_drain(mesh::Dispatcher *dispatcher);
void ble_bridge_maintain(void);
uint32_t ble_bridge_ms_until_next(void);
