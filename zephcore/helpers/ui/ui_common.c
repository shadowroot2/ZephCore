/*
 * ZephCore - UI Common
 * Copyright (c) 2025 ZephCore
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared ui_task.h implementations that are identical across all UI variants.
 * Compiled for every build that includes any UI (button, joystick, or future).
 */

#include "ui_task.h"

#ifdef CONFIG_ZEPHCORE_UI_BUZZER
#include "buzzer.h"
#endif

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ui_led, CONFIG_ZEPHCORE_BOARD_LOG_LEVEL);

/* ========== Startup Chime ========== */

void ui_play_startup_chime(void)
{
#ifdef CONFIG_ZEPHCORE_UI_BUZZER
	if (!buzzer_is_quiet()) {
		buzzer_play(MELODY_STARTUP);
	}
#endif
}

/* ========== LED Heartbeat ========== */
/*
 * Uses led0 (or led1 fallback) as a heartbeat indicator.
 * Pulse width extends to LED_ON_MSG_MS when there are unread messages,
 * driven by ui_led_get_msg_count() which the button variant overrides.
 *
 * led1 is also claimed as a message indicator in non-repeater companion builds
 * when both led0 and led1 are present. The heartbeat cycle turns led1 on
 * only when msg count > 0, giving a visual unread-message reminder.
 */

#if DT_NODE_HAS_PROP(DT_ALIAS(led0), gpios)
static const struct gpio_dt_spec s_heartbeat_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#define HAS_HEARTBEAT_LED 1
#elif DT_NODE_HAS_PROP(DT_ALIAS(led1), gpios)
static const struct gpio_dt_spec s_heartbeat_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
#define HAS_HEARTBEAT_LED 1
#else
#define HAS_HEARTBEAT_LED 0
#endif

/* Second LED for unread-message indication. Repeaters use led1 for LoRa TX
 * (via lora-tx-led alias) — no offline queue, so this is companion-only. */
#if HAS_HEARTBEAT_LED && DT_NODE_HAS_PROP(DT_ALIAS(led0), gpios) && \
    DT_NODE_HAS_PROP(DT_ALIAS(led1), gpios) && !defined(ZEPHCORE_REPEATER)
static const struct gpio_dt_spec s_msg_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
#define HAS_MSG_LED 1
#else
#define HAS_MSG_LED 0
#endif

#define LED_CYCLE_MS    4000   /* Total heartbeat period */
#define LED_ON_MS         20   /* Normal pulse width */
#define LED_ON_MSG_MS    200   /* Pulse width when unread messages */

#if HAS_HEARTBEAT_LED
static struct k_work_delayable s_led_on_work;
static struct k_work_delayable s_led_off_work;
static bool s_leds_disabled;

/*
 * Weak: returns current unread message count for pulse-width adaptation.
 * Overridden by ui_task.c (button UI) to read from ui_state.
 * Joystick UI leaves this at 0 — it drives its own message display.
 */
__attribute__((weak)) uint16_t ui_led_get_msg_count(void) { return 0; }

static void led_off_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	gpio_pin_set_dt(&s_heartbeat_led, 0);
#if HAS_MSG_LED
	gpio_pin_set_dt(&s_msg_led, 0);
#endif
	uint16_t on_ms = (ui_led_get_msg_count() > 0) ? LED_ON_MSG_MS : LED_ON_MS;

	k_work_reschedule(&s_led_on_work, K_MSEC(LED_CYCLE_MS - on_ms));
}

static void led_on_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	uint16_t mc = ui_led_get_msg_count();
	uint16_t on_ms = (mc > 0) ? LED_ON_MSG_MS : LED_ON_MS;

	if (!s_leds_disabled) {
		gpio_pin_set_dt(&s_heartbeat_led, 1);
#if HAS_MSG_LED
		if (mc > 0) {
			gpio_pin_set_dt(&s_msg_led, 1);
		}
#endif
	}
	k_work_reschedule(&s_led_off_work, K_MSEC(on_ms));
}
#endif /* HAS_HEARTBEAT_LED */

/*
 * Weak: called after s_leds_disabled changes so each UI variant can sync its
 * own display state. Overridden by ui_task.c (button UI) to update
 * ui_state->leds_disabled so the LEDs page shows the correct toggle state.
 */
__attribute__((weak)) void ui_led_on_disabled_changed(bool disabled) { ARG_UNUSED(disabled); }

void ui_led_heartbeat_init(void)
{
#if HAS_HEARTBEAT_LED
	if (gpio_is_ready_dt(&s_heartbeat_led)) {
		gpio_pin_configure_dt(&s_heartbeat_led, GPIO_OUTPUT_INACTIVE);
		k_work_init_delayable(&s_led_on_work, led_on_work_handler);
		k_work_init_delayable(&s_led_off_work, led_off_work_handler);
		k_work_reschedule(&s_led_on_work, K_NO_WAIT);
		LOG_INF("LED heartbeat started");
	}
#if HAS_MSG_LED
	if (gpio_is_ready_dt(&s_msg_led)) {
		gpio_pin_configure_dt(&s_msg_led, GPIO_OUTPUT_INACTIVE);
		LOG_INF("msg LED ready");
	}
#endif
#endif
}

void ui_set_heartbeat_led(bool enabled)
{
#if HAS_HEARTBEAT_LED
	if (enabled && !s_leds_disabled) {
		if (gpio_is_ready_dt(&s_heartbeat_led)) {
			k_work_reschedule(&s_led_on_work, K_NO_WAIT);
		}
	} else {
		k_work_cancel_delayable(&s_led_on_work);
		k_work_cancel_delayable(&s_led_off_work);
		gpio_pin_set_dt(&s_heartbeat_led, 0);
#if HAS_MSG_LED
		gpio_pin_set_dt(&s_msg_led, 0);
#endif
	}
#else
	(void)enabled;
#endif
}

void ui_set_leds_disabled(bool disabled)
{
#if HAS_HEARTBEAT_LED
	s_leds_disabled = disabled;
	if (disabled) {
		k_work_cancel_delayable(&s_led_on_work);
		k_work_cancel_delayable(&s_led_off_work);
		gpio_pin_set_dt(&s_heartbeat_led, 0);
#if HAS_MSG_LED
		gpio_pin_set_dt(&s_msg_led, 0);
#endif
	} else if (!k_work_delayable_is_pending(&s_led_on_work) &&
		   !k_work_delayable_is_pending(&s_led_off_work)) {
		/* Restart heartbeat only if it was stopped (avoids spurious pulse) */
		if (gpio_is_ready_dt(&s_heartbeat_led)) {
			k_work_reschedule(&s_led_on_work, K_NO_WAIT);
		}
	}
#else
	(void)disabled;
#endif
	ui_led_on_disabled_changed(disabled);
}

/* Flash the heartbeat LED immediately on message receipt.
 * Cancels the current cycle, pulses at LED_ON_MSG_MS width, then the
 * work chain resumes the normal heartbeat automatically.
 * No-op when LEDs are disabled or hardware is absent. */
void ui_led_flash_msg(void)
{
#if HAS_HEARTBEAT_LED
	if (!s_leds_disabled && gpio_is_ready_dt(&s_heartbeat_led)) {
		k_work_cancel_delayable(&s_led_on_work);
		k_work_cancel_delayable(&s_led_off_work);
		gpio_pin_set_dt(&s_heartbeat_led, 1);
		k_work_reschedule(&s_led_off_work, K_MSEC(LED_ON_MSG_MS));
	}
#endif
}

/* ========== Battery refresh ==========
 * Lazy: render path calls ui_refresh_battery(); ADC only fires when the
 * cached reading is older than UI_BATT_REFRESH_MS. Telemetry / stats paths
 * read fresh directly via their own callbacks — this gate only governs the
 * local display. */
#define UI_BATT_REFRESH_MS  30000

static uint16_t (*s_batt_provider)(void);
static uint32_t s_batt_last_read_ms;
static bool s_batt_ever_read;

void ui_set_battery_provider(uint16_t (*provider)(void))
{
	s_batt_provider = provider;
}

void ui_refresh_battery(void)
{
	if (!s_batt_provider) {
		return;
	}
	uint32_t now = k_uptime_get_32();
	if (s_batt_ever_read && (now - s_batt_last_read_ms) < UI_BATT_REFRESH_MS) {
		return;
	}
	ui_set_battery(s_batt_provider(), 0);
	s_batt_last_read_ms = k_uptime_get_32();
	s_batt_ever_read = true;
}

/* Forget the freshness timestamp so the next ui_refresh_battery() call is
 * guaranteed to sample the ADC. Use when entering a state where a fresh
 * reading matters (e.g. just woke the screen from sleep). */
void ui_invalidate_battery_cache(void)
{
	s_batt_ever_read = false;
	s_batt_last_read_ms = 0;
}

/* ========== Shared splash logo ==========
 * 128×13 ZephCore wordmark, MSB-first row-major (Adafruit XBM/drawBitmap
 * format). Used by both UI variants' splash renders via mc_display_xbm(). */
const uint8_t zephcore_logo[] = {
	0x00, 0x01, 0xff, 0x7f, 0xe7, 0xf8, 0x70, 0x70,
	0x3c, 0x01, 0xe0, 0x7f, 0xc3, 0xff, 0x00, 0x00,
	0x00, 0x01, 0xff, 0x7f, 0xe7, 0xfc, 0x70, 0x70,
	0xff, 0x07, 0xf8, 0x7f, 0xe3, 0xff, 0x00, 0x00,
	0x00, 0x01, 0xff, 0x7f, 0xe7, 0xfe, 0x70, 0x71,
	0xff, 0x0f, 0xfc, 0x7f, 0xf3, 0xff, 0x00, 0x00,
	0x00, 0x00, 0x0e, 0x70, 0x07, 0x0e, 0x70, 0x71,
	0xc7, 0x8e, 0x1c, 0x70, 0x73, 0x80, 0x00, 0x00,
	0x00, 0x00, 0x1c, 0x70, 0x07, 0x0e, 0x70, 0x73,
	0x83, 0x1c, 0x0e, 0x70, 0x73, 0x80, 0x00, 0x00,
	0x00, 0x00, 0x38, 0x7f, 0xe7, 0xfe, 0x7f, 0xf3,
	0x80, 0x1c, 0x0e, 0x7f, 0xf3, 0xff, 0x00, 0x00,
	0x00, 0x00, 0x78, 0x7f, 0xe7, 0xfc, 0x7f, 0xf3,
	0x80, 0x1c, 0x0e, 0x7f, 0xe3, 0xff, 0x00, 0x00,
	0x00, 0x00, 0x70, 0x7f, 0xe7, 0xf8, 0x7f, 0xf3,
	0x80, 0x1c, 0x0e, 0x7f, 0x83, 0xff, 0x00, 0x00,
	0x00, 0x00, 0xe0, 0x70, 0x07, 0x00, 0x70, 0x73,
	0x83, 0x1c, 0x0e, 0x73, 0xc3, 0x80, 0x00, 0x00,
	0x00, 0x01, 0xc0, 0x70, 0x07, 0x00, 0x70, 0x71,
	0xc7, 0x8e, 0x1c, 0x71, 0xe3, 0x80, 0x00, 0x00,
	0x00, 0x03, 0xff, 0x7f, 0xe7, 0x00, 0x70, 0x71,
	0xff, 0x0f, 0xfc, 0x70, 0xe3, 0xff, 0x00, 0x00,
	0x00, 0x03, 0xff, 0x7f, 0xe7, 0x00, 0x70, 0x70,
	0xff, 0x07, 0xf8, 0x70, 0xf3, 0xff, 0x00, 0x00,
	0x00, 0x03, 0xff, 0x7f, 0xe7, 0x00, 0x70, 0x70,
	0x3c, 0x01, 0xe0, 0x70, 0x7b, 0xff, 0x00, 0x00,
};
