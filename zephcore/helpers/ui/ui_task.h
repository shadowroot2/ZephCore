/*
 * ZephCore - UI Task
 * Copyright (c) 2025 ZephCore
 * SPDX-License-Identifier: MIT
 *
 * Wires together buttons, buzzer, and display.
 * Handles input events, page navigation, and notifications from mesh.
 *
 * All event-driven - no polling loops.
 */

#ifndef ZEPHCORE_UI_TASK_H
#define ZEPHCORE_UI_TASK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UI notification events (from mesh/BLE layer) */
enum ui_event {
	UI_EVENT_NONE = 0,
	UI_EVENT_CONTACT_MSG,    /* New contact message received */
	UI_EVENT_CHANNEL_MSG,    /* New channel message received */
	UI_EVENT_ROOM_MSG,       /* New room message received */
	UI_EVENT_ACK,            /* ACK received */
	UI_EVENT_APP_CONNECTED,  /* an app connected, on any transport */
	UI_EVENT_APP_DISCONNECTED, /* the last app session ended */
};

/* Companion transports with a client connected (ui_set_link_transports). */
#define UI_LINK_BLE  (1u << 0)
#define UI_LINK_USB  (1u << 1)  /* wired serial: a binary app or a text-CLI session */
#define UI_LINK_TCP  (1u << 2)  /* the WiFi companion's app socket */

/* WiFi companion station state (ui_set_wifi). */
enum ui_wifi_state {
	UI_WIFI_OFF = 0,  /* wifi.enabled 0, or no network set */
	UI_WIFI_JOINING,  /* joining, or waiting for a DHCP lease */
	UI_WIFI_UP,       /* associated with an IPv4 address */
};

/**
 * Initialize the UI subsystem.
 * Sets up display, buzzer, and input event callbacks.
 * Safe to call even if no display/buzzer hardware is present.
 *
 * @return 0 on success
 */
int ui_init(void);

/**
 * Play the startup chime if buzzer is not muted.
 * Call from main() AFTER loadPrefs() so the persisted buzzer_quiet
 * setting is respected. No chime if user has muted the buzzer.
 */
void ui_play_startup_chime(void);

/**
 * Initialize the LED heartbeat hardware.
 * Detects led0/led1 GPIO aliases, configures pins, and starts the
 * self-rescheduling work chain. Safe to call when no LED is present.
 * Must be called from ui_init() in every UI variant.
 */
void ui_led_heartbeat_init(void);

/**
 * Post a notification event to the UI.
 * Triggers buzzer melody and/or display wake depending on event type.
 * Safe to call from any thread context.
 *
 * @param event  The UI event type
 */
void ui_notify(enum ui_event event);

void ui_request_render(void); /* ZEPHCORE: explicit event-driven repaint */

/**
 * Update the message count shown on the messages page.
 *
 * @param count  Current unread message count
 */
void ui_set_msg_count(uint16_t count);

/**
 * Update radio parameters for display.
 */
void ui_set_radio_params(uint32_t freq_hz, uint8_t sf, uint16_t bw_khz_x10,
			 uint8_t cr, int8_t tx_power, int16_t noise_floor);

/**
 * Update extended live radio details for display.
 */
void ui_set_radio_runtime(uint8_t sync_word,
			  uint16_t preamble_len, bool rx_duty_cycle,
			  bool radio_ready, bool in_rx, bool tx_active);

/**
 * Update radio packet counters for display.
 */
void ui_set_radio_stats(uint32_t packets_rx, uint32_t packets_tx,
			uint32_t packets_err);

/**
 * Update GPS data for display.
 */
void ui_set_gps_data(bool has_fix, uint8_t sats,
		     int32_t lat_mdeg, int32_t lon_mdeg, int32_t alt_mm);

/**
 * Update battery data for display.
 */
void ui_set_battery(uint16_t mv, uint8_t pct);

/**
 * Update RTC clock for top bar display.
 * @param epoch  Unix timestamp (0 = time not set, hides clock)
 */
void ui_set_clock(uint32_t epoch);

/**
 * Set the whole-hour offset from UTC used when formatting the on-device
 * clock.  Display only: the epoch handed to ui_set_clock() stays UTC, and
 * nothing here may ever reach RTCClock or a packet timestamp.
 *
 * @param hours  Offset from UTC, TZ_OFFSET_MIN..TZ_OFFSET_MAX (0 = UTC)
 */
void ui_set_tz(int8_t hours);

/**
 * Record a recently heard contact for the "recent" page.
 *
 * @param name   Contact name (truncated to 15 chars)
 * @param rssi   Signal strength in dBm
 * @param age_s  Seconds since contact was heard (computed from RTC)
 */
void ui_add_recent(const char *name, int16_t rssi, uint32_t age_s);

/**
 * Set the node name for the display top bar.
 */
void ui_set_node_name(const char *name);

/**
 * Clear and rebuild the recently heard contact list.
 * Call ui_clear_recent() then ui_add_recent() for each entry.
 */
void ui_clear_recent(void);

/**
 * Update sensor data for display.
 */

/**
 * Set whether GPS hardware was detected at boot.
 * If false, GPS page shows "GPS: not detected".
 */
void ui_set_gps_available(bool available);

/**
 * Set GPS enabled state (for display page).
 */
void ui_set_gps_enabled(bool enabled);

/**
 * Update GPS state machine info for display.
 * @param state        0=OFF, 1=STANDBY (sleeping), 2=ACQUIRING (searching)
 * @param last_fix_age_s  Seconds since last fix (UINT32_MAX = never)
 * @param next_search_s   Seconds until next search (0 = now or off)
 */
void ui_set_gps_state(uint8_t state, uint32_t last_fix_age_s, uint32_t next_search_s);

/**
 * Set BLE enabled state (for display page).
 */
void ui_set_ble_enabled(bool enabled);

/**
 * Which companion transports have a client connected (UI_LINK_* bits).
 * Pushed on every link edge; the app-connected state itself comes from
 * UI_EVENT_APP_CONNECTED / _DISCONNECTED.
 */
void ui_set_link_transports(uint8_t mask);

/**
 * WiFi companion state for the WiFi page. ssid and ip are copied; either may
 * be NULL or empty. Repaints only when something changed.
 *
 * @param state  enum ui_wifi_state
 * @param port   TCP port the app connects to
 */
void ui_set_wifi(uint8_t state, const char *ssid, const char *ip, uint16_t port);

/** Persisted WiFi enable state shown by the WiFi page. */
void ui_set_wifi_enabled(bool enabled);

/**
 * Set notification mode (for display page).
 */
void ui_set_buzzer_mode(uint8_t mode);

/**
 * Set LEDs disabled state (for display page).
 */
void ui_set_leds_disabled(bool disabled);

void ui_set_bridge_enabled(bool enabled);
void ui_set_bridge_connected(bool connected);
void ui_set_bridge_status(const char *status);
void ui_set_bridge_addresses(const char *local, const char *peer);
void ui_set_bridge_metrics(uint8_t priority, uint32_t forwarded, uint32_t skipped);

bool ui_leds_disabled(void);

/**
 * Enable or disable the heartbeat LED.
 */
void ui_set_heartbeat_led(bool enabled);

void ui_led_set_ble_connected(bool connected);
void ui_led_set_ble_enabled(bool enabled);

/**
 * Flash the heartbeat LED immediately on message receipt.
 * Cancels the current cycle, pulses, then resumes normal heartbeat.
 */
void ui_led_flash_msg(void);

void ui_led_force_tx(void);
void ui_led_confirm_state(bool enabled);

/**
 * Flash the heartbeat LED 3 times as a visual shutdown indicator.
 * Used when the buzzer is muted — gives visual feedback on power-off.
 */
void ui_led_flash_shutdown(void);

void ui_shutdown(void);
bool ui_request_manual_shutdown(void);

/**
 * Set offgrid mode (client repeat) state for display page.
 */
void ui_set_offgrid_mode(bool enabled);

/**
 * Register a battery-voltage provider used by ui_refresh_battery().
 * provider() must return millivolts (0 if no battery hardware).
 */
void ui_set_battery_provider(uint16_t (*mv)(void), uint8_t (*pct)(void));

/**
 * Register the external-power provider used by charging indicators.
 */
void ui_set_power_source_provider(bool (*provider)(void));

/**
 * Lazy battery refresh: re-read the ADC only if cached value is stale.
 * Called from the page render path so the ADC fires at most once per
 * 30 s and only when the display is actually being drawn.
 */
void ui_refresh_battery(void);

/**
 * The low-battery warning screen, just before an auto-shutdown
 * (app/PowerPolicy.cpp). hold: keep it up 3 s on an OLED (it blanks
 * the moment power drops; e-paper keeps it anyway); false while the
 * main loop still has a notice to deliver. Nothing without a display.
 */
void ui_show_low_battery(bool hold);

bool ui_shutdown_in_progress(void);

/**
 * Drop the battery-refresh freshness timestamp. The next
 * ui_refresh_battery() call is guaranteed to sample the ADC.
 * Use when waking the display from sleep so the user sees a current
 * reading immediately instead of a possibly-stale cached value.
 */
void ui_invalidate_battery_cache(void);

/**
 * Notify UI of a received contact message.
 * Rich UIs display the text and sender; simpler ones forward to
 * ui_notify(UI_EVENT_CONTACT_MSG) + ui_set_msg_count().
 *
 * @param path_len   Hop count (OUT_PATH_UNKNOWN = direct/unknown)
 * @param from_name  Sender display name
 * @param text       Message text
 * @param msg_count  Updated offline queue message count
 */
void ui_notify_contact_msg(uint8_t path_len, const char *from_name,
			   const char *text, uint16_t msg_count);

/**
 * Notify UI of a received channel message.
 * Rich UIs use all parameters; simpler ones fire ui_notify(UI_EVENT_CHANNEL_MSG).
 *
 * @param channel_name  Human-readable channel name
 * @param text          Message text
 * @param ts            Sender timestamp (epoch)
 * @param path_len      Hop count (OUT_PATH_UNKNOWN = direct/unknown)
 * @param msg_count     Updated offline queue message count
 */
void ui_notify_channel_msg(const char *channel_name, const char *text,
			   uint32_t ts, uint8_t path_len, uint16_t msg_count);

/**
 * Notify UI that an outbound packet was transmitted.
 * Rich UIs use this to start RTT timers; simpler ones ignore it.
 */
void ui_notify_packet_sent(void);

/* ===== Input axis flip =====
 * For an upside-down mount: flips the two joystick/D-pad axes back. Separate
 * from the display rotation on purpose. State in ui_common.c; each variant's
 * input callback maps its raw code through zephcore_input_map_code(). */

/**
 * Enable or disable the joystick/D-pad axis swap.
 *
 * @param flipped true to swap up/down and left/right
 */
void zephcore_input_set_flipped(bool flipped);

/**
 * @return true if the input axes are currently swapped.
 */
bool zephcore_input_is_flipped(void);

/**
 * Map a raw Zephyr INPUT_KEY_* code through the current axis flip.
 * Returns @p code unchanged when the flip is off or the code is not
 * a directional key.
 *
 * @param code Raw input event code
 * @return The code the UI should decode
 */
uint16_t zephcore_input_map_code(uint16_t code);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHCORE_UI_TASK_H */
