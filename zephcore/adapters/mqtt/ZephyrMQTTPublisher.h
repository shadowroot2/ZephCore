/*
 * SPDX-License-Identifier: MIT
 * ZephyrMQTTPublisher — MQTT client thread for the Observer role.
 *
 * Dedicated thread: waits for WiFi ready, resolves broker hostname,
 * connects with optional TLS (TLS_PEER_VERIFY_NONE), publishes LWT and
 * status, then drains a pre-serialized JSON publish queue.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ObserverCreds;

/*
 * Start the MQTT publisher thread.
 *   creds         — runtime credentials; pointer kept, must remain valid.
 *   client_id     — MQTT client identifier string (e.g. "Observer-AABBCCDD").
 *   status_topic  — topic for retained online/offline status messages.
 *   packets_topic — topic for received LoRa packet JSON messages.
 *
 * Must be called once from main() after wifi_station_start().
 */
void mqtt_publisher_start(const struct ObserverCreds *creds,
			  const char *client_id,
			  const char *status_topic,
			  const char *packets_topic);

/*
 * Enqueue a pre-serialized JSON payload for publishing.
 * topic and payload are copied — safe to pass stack buffers.
 * Drops the message silently if the queue is full.
 * Safe to call from any thread.
 */
void mqtt_publisher_enqueue(const char *topic, const char *payload, int payload_len);

/* Returns true when the MQTT session is active. */
bool mqtt_publisher_is_connected(void);

/*
 * Trigger an MQTT reconnect (e.g. after credential change via CLI).
 * Safe to call from any thread.
 */
void mqtt_publisher_reconnect(void);

/*
 * Register a callback invoked once each time MQTT connects (CONNACK received).
 * Called from the MQTT publisher thread — keep it short.
 * Pass NULL to clear.
 */
void mqtt_publisher_set_connect_cb(void (*cb)(void));

#ifdef __cplusplus
}
#endif
