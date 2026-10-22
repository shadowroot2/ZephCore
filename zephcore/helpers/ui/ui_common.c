/*
 * ZephCore - UI Common
 * Copyright (c) 2025 ZephCore
 * SPDX-License-Identifier: MIT
 *
 * Shared ui_task.h implementations that are identical across all UI variants.
 * Compiled for every build that includes any UI (button, joystick, or future).
 */

#include "ui_task.h"

#ifdef CONFIG_ZEPHCORE_UI_BUZZER
#include "buzzer.h"
#endif

#ifdef CONFIG_ZEPHCORE_UI_DISPLAY
#include "display.h"
#endif

#include <zephyr_poweroff.h>
#include "led_gate.h"               /* shared with the LoRa TX LED */
#include <helpers/battery_curve.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/kernel.h>
#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ui_led, CONFIG_ZEPHCORE_BOARD_LOG_LEVEL);

/* ========== Input axis flip ==========
 * Shared by both UI variants. Written from the mesh/CLI thread, read from the
 * input callback; a plain bool is enough. */

static bool input_flipped;

void zephcore_input_set_flipped(bool flipped)
{
	input_flipped = flipped;
}

bool zephcore_input_is_flipped(void)
{
	return input_flipped;
}

uint16_t zephcore_input_map_code(uint16_t code)
{
	if (!input_flipped) {
		return code;
	}

	switch (code) {
	case INPUT_KEY_UP:    return INPUT_KEY_DOWN;
	case INPUT_KEY_DOWN:  return INPUT_KEY_UP;
	case INPUT_KEY_LEFT:  return INPUT_KEY_RIGHT;
	case INPUT_KEY_RIGHT: return INPUT_KEY_LEFT;
	default:              return code;
	}
}

static uint16_t (*s_batt_provider)(void);
static bool (*s_power_source_provider)(void);

/* Low-charge power-saving threshold shared by the buzzer and heartbeat. */
#define BUZZER_LOW_BATT_THRESHOLD_PCT  25


/* ========== Startup Chime ========== */

void ui_play_startup_chime(void)
{
#ifdef CONFIG_ZEPHCORE_UI_BUZZER
	/* The startup melody comes before the periodic heartbeat sample. Check the
	 * cell now so a weak battery never spends energy on a boot chime. */
	if (s_batt_provider) {
		uint16_t mv = s_batt_provider();
		bool low = mv != 0 && battery_curve_lookup(&battery_curve_default, mv) <
			BUZZER_LOW_BATT_THRESHOLD_PCT;
		buzzer_set_low_battery_quiet(low);

	}
	if (!buzzer_is_quiet()) {
		buzzer_play(MELODY_STARTUP);
	}
#endif
}

/* ========== LED Heartbeat ========== */
/*
 * Heartbeat on led0 (or led1). The pulse widens while there are unread
 * messages; with both LEDs present, companions light led1 for unread.
 */

/* PWM takes priority over the plain led0/led1 fallback. */
#if DT_NODE_EXISTS(DT_ALIAS(heartbeat_pwm_led))
static const struct pwm_dt_spec s_heartbeat_led_pwm =
	PWM_DT_SPEC_GET(DT_ALIAS(heartbeat_pwm_led));
#define HAS_HEARTBEAT_LED_PWM 1
#define HAS_HEARTBEAT_LED 0
#elif DT_NODE_HAS_PROP(DT_ALIAS(led0), gpios)
static const struct gpio_dt_spec s_heartbeat_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#define HAS_HEARTBEAT_LED_PWM 0
#define HAS_HEARTBEAT_LED 1
#elif DT_NODE_HAS_PROP(DT_ALIAS(led1), gpios)
static const struct gpio_dt_spec s_heartbeat_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
#define HAS_HEARTBEAT_LED_PWM 0
#define HAS_HEARTBEAT_LED 1
#else
#define HAS_HEARTBEAT_LED_PWM 0
#define HAS_HEARTBEAT_LED 0
#endif

#define HAS_ANY_HEARTBEAT_LED (HAS_HEARTBEAT_LED_PWM || HAS_HEARTBEAT_LED)

#if HAS_HEARTBEAT_LED_PWM
static inline bool heartbeat_led_is_ready(void)
{
	return pwm_is_ready_dt(&s_heartbeat_led_pwm);
}
static inline void heartbeat_led_write(bool on)
{
	zephcore_led_pwm_write(&s_heartbeat_led_pwm, on);
}
#elif HAS_HEARTBEAT_LED
static inline bool heartbeat_led_is_ready(void)
{
	return gpio_is_ready_dt(&s_heartbeat_led);
}
static inline void heartbeat_led_write(bool on)
{
	gpio_pin_set_dt(&s_heartbeat_led, on ? 1 : 0);
}
#endif

/* Optional red low-battery indicator. */
#if DT_NODE_HAS_PROP(DT_ALIAS(low_battery_led), gpios)
static const struct gpio_dt_spec s_low_battery_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(low_battery_led), gpios);
#define HAS_LOW_BATTERY_LED 1
#else
#define HAS_LOW_BATTERY_LED 0
#endif

#if defined(CONFIG_BOARD_THINKNODE_M3) && \
	DT_NODE_HAS_PROP(DT_ALIAS(charge_usb_detect), gpios)
static const struct gpio_dt_spec s_charge_usb_detect =
	GPIO_DT_SPEC_GET(DT_ALIAS(charge_usb_detect), gpios);
#define HAS_M3_CHARGE_STATUS 1
#else
#define HAS_M3_CHARGE_STATUS 0
#endif

#if defined(CONFIG_BOARD_XIAO_NRF52840) && defined(ZEPHCORE_REPEATER) && \
	DT_NODE_HAS_PROP(DT_ALIAS(led1), gpios)
static const struct gpio_dt_spec s_charge_full_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
#define HAS_XIAO_CHARGE_STATUS 1
#else
#define HAS_XIAO_CHARGE_STATUS 0
#endif

#if defined(CONFIG_BOARD_LILYGO_TECHO)
#define HAS_TECHO_CHARGE_STATUS 1
#else
#define HAS_TECHO_CHARGE_STATUS 0
#endif
#define HAS_M3_STYLE_CHARGE_STATUS (HAS_M3_CHARGE_STATUS || HAS_TECHO_CHARGE_STATUS)
#define HAS_RGB_CHARGE_STATUS (HAS_M3_STYLE_CHARGE_STATUS || HAS_XIAO_CHARGE_STATUS)

#if defined(CONFIG_BOARD_T1000_E) && \
	DT_NODE_HAS_PROP(DT_ALIAS(charge_usb_detect), gpios)
static const struct gpio_dt_spec s_charge_usb_detect =
	GPIO_DT_SPEC_GET(DT_ALIAS(charge_usb_detect), gpios);
#define HAS_T1000_CHARGE_STATUS 1
#else
#define HAS_T1000_CHARGE_STATUS 0
#endif

/* Second LED for unread-message indication. Repeaters use led1 for LoRa TX
 * (via lora-tx-led alias) — no offline queue, so this is companion-only. */
#if HAS_ANY_HEARTBEAT_LED && DT_NODE_HAS_PROP(DT_ALIAS(led0), gpios) && \
	DT_NODE_HAS_PROP(DT_ALIAS(led1), gpios) && !defined(ZEPHCORE_REPEATER)
static const struct gpio_dt_spec s_msg_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
#define HAS_MSG_LED 1
#else
#define HAS_MSG_LED 0
#endif

#if DT_NODE_HAS_PROP(DT_ALIAS(ble_status_led), gpios)
static const struct gpio_dt_spec s_ble_status_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(ble_status_led), gpios);
#define HAS_BLE_STATUS_LED 1
#else
#define HAS_BLE_STATUS_LED 0
#endif

#if DT_NODE_HAS_PROP(DT_ALIAS(led_enable), gpios)
static const struct gpio_dt_spec s_led_enable =
	GPIO_DT_SPEC_GET(DT_ALIAS(led_enable), gpios);
#define HAS_LED_ENABLE 1
#else
#define HAS_LED_ENABLE 0
#endif

#if HAS_BLE_STATUS_LED && DT_SAME_NODE(DT_ALIAS(ble_status_led), DT_ALIAS(led0))
#define HEARTBEAT_IS_BLE_STATUS_LED 1
#else
#define HEARTBEAT_IS_BLE_STATUS_LED 0
#endif

#define LED_CYCLE_MS                 5000  /* Heartbeat period */
#define LED_ON_MS                      20  /* Normal pulse width */
#define LED_ON_MSG_MS                 200  /* Pulse width when unread messages */
#define LED_HEARTBEAT_BLINKS            1
#if defined(CONFIG_BOARD_THINKNODE_M3) || defined(CONFIG_BOARD_LILYGO_TECHO)
#define LED_UNREAD_BLINKS               2
#else
#define LED_UNREAD_BLINKS               1
#endif
#define LED_HEARTBEAT_BLINK_GAP_MS      80
#if defined(CONFIG_BOARD_THINKNODE_M3) || defined(CONFIG_BOARD_LILYGO_TECHO)
#define LED_LOW_BATT_THRESHOLD_PCT     20
#elif defined(ZEPHCORE_REPEATER)
#define LED_LOW_BATT_THRESHOLD_PCT     15
#else
#define LED_LOW_BATT_THRESHOLD_PCT     25
#endif
#define LED_LOW_BATT_BLINK_MS           80
#define LED_LOW_BATT_BLINKS              3
#define LED_CHARGING_BLINK_MS          1000
#define CHARGE_FULL_MV                 4190
#define CHARGE_PERCENT_SAMPLE_MS       30000U
#define CHARGE_PERCENT_BLINK_MS          150
#define CHARGE_PERCENT_BLINK_GAP_MS      200
#define CHARGE_PERCENT_SERIES_PAUSE_MS  1500
#if defined(CONFIG_BOARD_T1000_E)
#define LED_MSG_BLINK_MS   80  /* Fast incoming-message flash phase */
#define LED_MSG_BLINKS      3  /* Number of flashes for one incoming message */
#endif

#if HAS_ANY_HEARTBEAT_LED
static struct k_work_delayable s_led_on_work;
static struct k_work_delayable s_led_off_work;
static uint8_t s_heartbeat_blinks_left;
static uint8_t s_heartbeat_blinks_total;
static uint16_t s_heartbeat_on_ms;
static bool s_heartbeat_low_batt_cycle;
static bool s_heartbeat_charging_cycle;
#if HAS_RGB_CHARGE_STATUS || HAS_T1000_CHARGE_STATUS
static bool s_heartbeat_charge_percent_cycle;
#endif
#if defined(CONFIG_BOARD_T1000_E)
static struct k_work_delayable s_msg_blink_work;
static struct k_work_delayable s_tx_led_off_work;
static uint8_t s_msg_blink_phase;
static uint8_t s_msg_blink_count;
static bool s_tx_led_active;
#endif
#if HAS_BLE_STATUS_LED
static bool s_ble_enabled = true;
static bool s_ble_connected;
#endif

/*
 * Weak: returns current unread message count for pulse-width adaptation.
 * Overridden by ui_task.c (button UI) to read from ui_state.
 * Joystick UI leaves this at 0 — it drives its own message display.
 */
__attribute__((weak)) uint16_t ui_led_get_msg_count(void) { return 0; }

/*
 * Does the heartbeat LED light on this pass? The cycle keeps running in
 * every mode, LEDS_HB_OFF included, because the unread indicator is lit from
 * inside it.
 */
static bool hb_should_light(uint16_t msg_count)
{
	switch (zephcore_leds_hb_mode()) {
	case LEDS_HB_OFF:    return false;
	case LEDS_HB_UNREAD: return msg_count > 0;  /* dark unless there is news */
	default:             return true;           /* LEDS_HB_ALL, LEDS_HB_HB */
	}
}

/* Pulse width for this pass.  LEDS_HB_HB is the "liveness tick only" mode, so
 * it never widens even when messages are waiting. */
static uint16_t hb_pulse_ms(uint16_t msg_count)
{
	if (zephcore_leds_hb_mode() == LEDS_HB_HB) {
		return LED_ON_MS;
	}
	return (msg_count > 0) ? LED_ON_MSG_MS : LED_ON_MS;
}

/* The heartbeat is not a precise fuel gauge; it only needs a stable visual
 * low-charge indication. Refresh its voltage estimate at the same cadence as
 * the UI, rather than waking the ADC on every LED pulse. */
static bool heartbeat_low_battery(void)
{
	static uint32_t last_sample_ms;
	static bool sampled;
	static bool low;
	uint32_t now = k_uptime_get_32();

	if (s_power_source_provider && s_power_source_provider()) {
		return false;
	}
	if (!s_batt_provider) {
		return false;
	}
	if (!sampled || (now - last_sample_ms) >= 30000U) {
		uint16_t mv = s_batt_provider();
		uint8_t pct = battery_curve_lookup(&battery_curve_default, mv);
		low = mv != 0 && pct < LED_LOW_BATT_THRESHOLD_PCT;
#ifdef CONFIG_ZEPHCORE_UI_BUZZER
		/* Keep the user's buzzer preference intact: this is a temporary
		 * low-charge override and is lifted automatically on recovery. */
		buzzer_set_low_battery_quiet(mv != 0 && pct < BUZZER_LOW_BATT_THRESHOLD_PCT);

#endif
		last_sample_ms = now;
		sampled = true;
	}
	return low;
}

#if HAS_T1000_CHARGE_STATUS
static bool charge_gpio_is_active(const struct gpio_dt_spec *spec)
{
	int level = gpio_pin_get_dt(spec);

	if (level < 0) {
		return false;
	}
	return (spec->dt_flags & GPIO_ACTIVE_LOW) ? level == 0 : level != 0;
}
#endif

#if HAS_RGB_CHARGE_STATUS
enum m3_charge_state {
	M3_CHARGE_NONE,
	M3_CHARGE_ACTIVE,
	M3_CHARGE_FULL,
};

#if !HAS_TECHO_CHARGE_STATUS
static uint8_t s_m3_charge_pct;
#endif

#if HAS_M3_CHARGE_STATUS
static bool m3_gpio_is_active(const struct gpio_dt_spec *spec)
{
	int level = gpio_pin_get_dt(spec);

	if (level < 0) {
		return false;
	}
	return (spec->dt_flags & GPIO_ACTIVE_LOW) ? level == 0 : level != 0;
}
#endif

static enum m3_charge_state m3_charge_state_get(void)
{
#if HAS_TECHO_CHARGE_STATUS
	/* T-ECHO's ADC rises with VBUS (4.841 V observed at 68% on battery).
	 * USB presence is reliable; charge percentage/completion is not. */
	return s_power_source_provider && s_power_source_provider() ?
		M3_CHARGE_ACTIVE : M3_CHARGE_NONE;
#else
	static uint32_t last_voltage_sample_ms;
	static bool full_voltage;
	static bool voltage_sampled;
#if HAS_XIAO_CHARGE_STATUS || HAS_TECHO_CHARGE_STATUS
	bool usb_present_now = s_power_source_provider && s_power_source_provider();
#else
	bool usb_present_now = gpio_is_ready_dt(&s_charge_usb_detect) &&
		m3_gpio_is_active(&s_charge_usb_detect);
#endif
	uint32_t now = k_uptime_get_32();

	if (!usb_present_now) {
		full_voltage = false;
		voltage_sampled = false;
		s_m3_charge_pct = 0;
		return M3_CHARGE_NONE;
	}
	if (s_batt_provider &&
	    (!voltage_sampled || (now - last_voltage_sample_ms) >= CHARGE_PERCENT_SAMPLE_MS)) {
		uint16_t mv = s_batt_provider();

		last_voltage_sample_ms = now;
		voltage_sampled = true;
		s_m3_charge_pct = mv ? battery_curve_lookup(&battery_curve_default, mv) : 0;
		/* Use each board's curve for full-charge indication. */
		full_voltage = mv != 0 && s_m3_charge_pct == 100;
	}
	return full_voltage ? M3_CHARGE_FULL : M3_CHARGE_ACTIVE;
#endif
}

#if !HAS_TECHO_CHARGE_STATUS
static uint8_t m3_charge_blink_count(void)
{
	return s_m3_charge_pct < 50 ? 1 : (s_m3_charge_pct < 75 ? 2 : 3);
}
#endif
#endif

#if HAS_XIAO_CHARGE_STATUS
/* Green has its own cycle; red heartbeat and blue TX remain independent. */
static void xiao_charge_work_handler(struct k_work *work)
{
	static uint32_t series_start;
	static uint8_t blinks;
	uint32_t now = k_uptime_get_32();
	enum m3_charge_state state = m3_charge_state_get();
	bool on = false;
	uint32_t delay_ms = 1000;

	if (zephcore_leds_disabled() || state == M3_CHARGE_NONE) {
		blinks = 0;
	} else if (state == M3_CHARGE_FULL) {
		blinks = 0;
		on = true;
	} else {
		if (blinks == 0) {
			blinks = m3_charge_blink_count();
			series_start = now;
		}
		uint32_t step_ms = CHARGE_PERCENT_BLINK_MS + CHARGE_PERCENT_BLINK_GAP_MS;
		uint32_t burst_ms = blinks * step_ms - CHARGE_PERCENT_BLINK_GAP_MS;
		uint32_t elapsed = now - series_start;
		if (elapsed >= burst_ms + CHARGE_PERCENT_SERIES_PAUSE_MS) {
			blinks = m3_charge_blink_count();
			series_start = now;
			elapsed = 0;
			burst_ms = blinks * step_ms - CHARGE_PERCENT_BLINK_GAP_MS;
		}
		on = elapsed < burst_ms && elapsed % step_ms < CHARGE_PERCENT_BLINK_MS;
		if (elapsed < burst_ms) {
			uint32_t phase_ms = elapsed % step_ms;
			delay_ms = on ? CHARGE_PERCENT_BLINK_MS - phase_ms : step_ms - phase_ms;
		} else {
			delay_ms = MIN(1000U, burst_ms + CHARGE_PERCENT_SERIES_PAUSE_MS - elapsed);
		}
	}
	/* Recheck the master switch before touching the GPIO. */
	gpio_pin_set_dt(&s_charge_full_led, on && !zephcore_leds_disabled());
	k_work_reschedule(k_work_delayable_from_work(work), K_MSEC(delay_ms));
}
K_WORK_DELAYABLE_DEFINE(s_xiao_charge_work, xiao_charge_work_handler);
#endif

#if HAS_T1000_CHARGE_STATUS
enum t1000_charge_state {
	T1000_CHARGE_NONE,
	T1000_CHARGE_ACTIVE,
	T1000_CHARGE_FULL,
};

static uint8_t s_t1000_charge_pct;

static enum t1000_charge_state t1000_charge_state_get(void)
{
	static uint32_t last_voltage_sample_ms;
	static bool full_voltage;
	static bool voltage_sampled;
	bool usb_present_now = gpio_is_ready_dt(&s_charge_usb_detect) &&
		charge_gpio_is_active(&s_charge_usb_detect);
	uint32_t now = k_uptime_get_32();

	if (!usb_present_now) {
		full_voltage = false;
		voltage_sampled = false;
		s_t1000_charge_pct = 0;
		return T1000_CHARGE_NONE;
	}

	if (s_batt_provider &&
	    (!voltage_sampled || (now - last_voltage_sample_ms) >= CHARGE_PERCENT_SAMPLE_MS)) {
		uint16_t mv = s_batt_provider();

		last_voltage_sample_ms = now;
		voltage_sampled = true;
		full_voltage = mv >= CHARGE_FULL_MV;
		s_t1000_charge_pct = mv ? battery_curve_lookup(&battery_curve_default, mv) : 0;
	}
	return full_voltage ? T1000_CHARGE_FULL : T1000_CHARGE_ACTIVE;
}

static uint8_t t1000_charge_blink_count(void)
{
	return s_t1000_charge_pct < 50 ? 1 : (s_t1000_charge_pct < 75 ? 2 : 3);
}
#endif

bool zephcore_led_status_priority_active(void)
{
#if HAS_M3_STYLE_CHARGE_STATUS
	return m3_charge_state_get() != M3_CHARGE_NONE;
#elif HAS_T1000_CHARGE_STATUS
	return t1000_charge_state_get() != T1000_CHARGE_NONE;
#else
	return false;
#endif
}

static void heartbeat_sequence_reset(void)
{
	s_heartbeat_blinks_left = 0;
	s_heartbeat_blinks_total = 0;
	s_heartbeat_on_ms = 0;
	s_heartbeat_low_batt_cycle = false;
	s_heartbeat_charging_cycle = false;
#if HAS_RGB_CHARGE_STATUS || HAS_T1000_CHARGE_STATUS
	s_heartbeat_charge_percent_cycle = false;
#endif
}

static void heartbeat_led_set(bool on)
{
	if (zephcore_led_radio_holds_pin() && !zephcore_led_status_priority_active()) {
		return;
	}
	bool use_low_battery_led = on &&
		(s_heartbeat_low_batt_cycle || s_heartbeat_charging_cycle) && HAS_LOW_BATTERY_LED;

#if HAS_LED_ENABLE
	if (on) {
		gpio_pin_set_dt(&s_led_enable, 1);
	}
#endif
	heartbeat_led_write(on && !use_low_battery_led);
#if HAS_LOW_BATTERY_LED
	gpio_pin_set_dt(&s_low_battery_led, use_low_battery_led ? 1 : 0);
#endif
#if HAS_LED_ENABLE
	if (!on) {
		gpio_pin_set_dt(&s_led_enable, 0);
	}
#endif
}


static void led_off_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	/* Yield the pin if radio activity is holding it (shared-pin boards only;
	 * everywhere else this always reads false). Clearing here would blank the
	 * LED in the middle of a transmit. Routed through heartbeat_led_write()
	 * rather than a direct gpio_pin_set_dt(), since s_heartbeat_led doesn't
	 * even exist on PWM-capable boards (see HAS_HEARTBEAT_LED_PWM above). */
	if (!zephcore_led_radio_holds_pin()) {
		heartbeat_led_write(false);
	}

#if HAS_MSG_LED
	#if !defined(CONFIG_BOARD_THINKNODE_M1)
	gpio_pin_set_dt(&s_msg_led, 0);
	#else
	if (!zephcore_led_radio_holds_pin()) gpio_pin_set_dt(&s_msg_led, 0);
	#endif
#endif
#if HAS_BLE_STATUS_LED
	#if !defined(CONFIG_BOARD_THINKNODE_M1)
	gpio_pin_set_dt(&s_ble_status_led, 0);
	#else
	if (!zephcore_led_radio_holds_pin()) gpio_pin_set_dt(&s_ble_status_led, 0);
	#endif
#endif

	if (s_heartbeat_blinks_left > 0) {
		s_heartbeat_blinks_left--;
	}
	if (s_heartbeat_blinks_left > 0) {
		/* Continue the current heartbeat burst after a short dark gap. */
		uint32_t gap_ms = s_heartbeat_low_batt_cycle ? LED_LOW_BATT_BLINK_MS :
			LED_HEARTBEAT_BLINK_GAP_MS;
#if HAS_RGB_CHARGE_STATUS || HAS_T1000_CHARGE_STATUS
		if (s_heartbeat_charge_percent_cycle) {
			gap_ms = CHARGE_PERCENT_BLINK_GAP_MS;
		}
#endif
		k_work_reschedule(&s_led_on_work, K_MSEC(gap_ms));
		return;
	}


	uint32_t delay_ms;
	#if HAS_RGB_CHARGE_STATUS || HAS_T1000_CHARGE_STATUS
	if (s_heartbeat_charge_percent_cycle) {
		delay_ms = CHARGE_PERCENT_SERIES_PAUSE_MS;
	} else
	#endif
	if (s_heartbeat_charging_cycle) {
		/* Red: one second on, one second off while charging. */
		delay_ms = LED_CHARGING_BLINK_MS;
	} else if (s_heartbeat_low_batt_cycle) {
		/* Three 80 ms flashes, separated by two 80 ms gaps, start once
		 * every five seconds. */
		delay_ms = LED_CYCLE_MS -
			(LED_LOW_BATT_BLINKS * LED_LOW_BATT_BLINK_MS +
			 (LED_LOW_BATT_BLINKS - 1U) * LED_LOW_BATT_BLINK_MS);
	} else if (s_heartbeat_blinks_total > 0) {
		uint32_t burst_ms = s_heartbeat_blinks_total * s_heartbeat_on_ms +
			(s_heartbeat_blinks_total - 1U) * LED_HEARTBEAT_BLINK_GAP_MS;
		delay_ms = LED_CYCLE_MS - burst_ms;
	} else {
		/* A one-off incoming-message flash interrupted the heartbeat. */
		delay_ms = LED_CYCLE_MS - LED_ON_MSG_MS;
	}
	heartbeat_sequence_reset();
	k_work_reschedule(&s_led_on_work, K_MSEC(delay_ms));
}

static void led_on_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
#if HAS_T1000_CHARGE_STATUS
	if (s_tx_led_active) {
		if (t1000_charge_state_get() == T1000_CHARGE_NONE) {
			k_work_reschedule(&s_led_on_work, K_MSEC(100));
			return;
		}
		k_work_cancel_delayable(&s_tx_led_off_work);
		s_tx_led_active = false;
	}
#endif
	uint16_t mc = ui_led_get_msg_count();
	if (zephcore_leds_hb_mode() == LEDS_HB_HB) {
		mc = 0;
	}
	bool m3_charging = false;

#if HAS_M3_STYLE_CHARGE_STATUS
	enum m3_charge_state m3_charge_state = m3_charge_state_get();
	if (m3_charge_state == M3_CHARGE_FULL) {
		heartbeat_sequence_reset();
		if (!zephcore_leds_disabled()) {
			/* USB attached and cell full: solid green. */
			heartbeat_led_set(true);
		}
		k_work_reschedule(&s_led_on_work, K_SECONDS(1));
		return;
	}
	m3_charging = m3_charge_state == M3_CHARGE_ACTIVE;
#if HAS_TECHO_CHARGE_STATUS
	if (!m3_charging && s_heartbeat_charging_cycle) {
		heartbeat_sequence_reset();
	}
#endif
	if (m3_charging && (s_heartbeat_blinks_left == 0 ||
			   (HAS_TECHO_CHARGE_STATUS && !s_heartbeat_charging_cycle))) {
		/* Do not finish a queued heartbeat/unread-message sequence in green.
		 * Charging owns the RGB LED immediately and exclusively. */
		heartbeat_sequence_reset();
		s_heartbeat_charging_cycle = true;
#if HAS_TECHO_CHARGE_STATUS
		/* Red USB indicator only; the charger's own LED reports completion. */
		s_heartbeat_blinks_total = 1;
		s_heartbeat_on_ms = LED_CHARGING_BLINK_MS;
#else
		s_heartbeat_charge_percent_cycle = true;
		s_heartbeat_blinks_total = m3_charge_blink_count();
		s_heartbeat_on_ms = CHARGE_PERCENT_BLINK_MS;
#endif
		s_heartbeat_blinks_left = s_heartbeat_blinks_total;
		if (!zephcore_leds_disabled()) {
			heartbeat_led_set(true);
		}
		k_work_reschedule(&s_led_off_work, K_MSEC(s_heartbeat_on_ms));
		return;
	}
#endif

#if HAS_T1000_CHARGE_STATUS
	enum t1000_charge_state t1000_charge_state = t1000_charge_state_get();
	if (t1000_charge_state == T1000_CHARGE_FULL) {
		heartbeat_sequence_reset();
		if (!zephcore_leds_disabled()) {
			heartbeat_led_set(true);
		}
		k_work_reschedule(&s_led_on_work, K_SECONDS(1));
		return;
	}
	if (t1000_charge_state == T1000_CHARGE_ACTIVE && s_heartbeat_blinks_left == 0) {
		heartbeat_sequence_reset();
		s_heartbeat_charging_cycle = true;
		s_heartbeat_charge_percent_cycle = true;
		s_heartbeat_blinks_total = t1000_charge_blink_count();
		s_heartbeat_blinks_left = s_heartbeat_blinks_total;
		s_heartbeat_on_ms = CHARGE_PERCENT_BLINK_MS;
		if (!zephcore_leds_disabled()) {
			heartbeat_led_set(true);
		}
		k_work_reschedule(&s_led_off_work, K_MSEC(CHARGE_PERCENT_BLINK_MS));
		return;
	}
#endif

	if (s_heartbeat_blinks_left == 0) {
		s_heartbeat_charging_cycle = m3_charging;
		s_heartbeat_low_batt_cycle = !m3_charging && heartbeat_low_battery();
		s_heartbeat_blinks_total = s_heartbeat_charging_cycle ? 1 :
			(s_heartbeat_low_batt_cycle ? LED_LOW_BATT_BLINKS :
			 (mc > 0 ? LED_UNREAD_BLINKS : LED_HEARTBEAT_BLINKS));
		s_heartbeat_blinks_left = s_heartbeat_blinks_total;
		s_heartbeat_on_ms = s_heartbeat_charging_cycle ? LED_CHARGING_BLINK_MS :
			(s_heartbeat_low_batt_cycle ? LED_LOW_BATT_BLINK_MS :
			 hb_pulse_ms(mc));
	}


	if (!zephcore_leds_disabled() &&
	    (s_heartbeat_charging_cycle || hb_should_light(mc))) {
		#if HAS_BLE_STATUS_LED && !defined(CONFIG_BOARD_LILYGO_TECHO)
		bool ble_waiting = s_ble_enabled && !s_ble_connected;
		#endif
		#if defined(CONFIG_BOARD_LILYGO_TECHO) && HAS_MSG_LED && HAS_BLE_STATUS_LED
		/* T-ECHO: green means unread, blue means no BLE peer, red means
		 * connected and no unread messages. The cadence is unchanged. */
		if (mc > 0) {

			gpio_pin_set_dt(&s_msg_led, 1);
		} else if (!s_ble_connected) {
			gpio_pin_set_dt(&s_ble_status_led, 1);
		} else {
			heartbeat_led_set(true);
		}
		#elif !HEARTBEAT_IS_BLE_STATUS_LED
		heartbeat_led_set(true);
		#else
		if (ble_waiting) {
			heartbeat_led_set(true);
		}
		#endif
#if HAS_MSG_LED && !defined(CONFIG_BOARD_LILYGO_TECHO)
		if (mc > 0 && !s_heartbeat_charging_cycle) {
			#if defined(CONFIG_BOARD_THINKNODE_M1)
			if (!zephcore_led_radio_holds_pin())
			#endif
			gpio_pin_set_dt(&s_msg_led, 1);
		}
#endif
		#if HAS_BLE_STATUS_LED && !HEARTBEAT_IS_BLE_STATUS_LED && \
			!defined(CONFIG_BOARD_LILYGO_TECHO)
		if (ble_waiting) {
			#if defined(CONFIG_BOARD_THINKNODE_M1)
			if (!zephcore_led_radio_holds_pin())
			#endif
			gpio_pin_set_dt(&s_ble_status_led, 1);
		}
		#endif
	}
	k_work_reschedule(&s_led_off_work, K_MSEC(s_heartbeat_on_ms));
}

#if defined(CONFIG_BOARD_T1000_E)
static void msg_blink_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	if (zephcore_led_status_priority_active()) {
		k_work_reschedule(&s_led_on_work, K_NO_WAIT);
		return;
	}
	if ((s_msg_blink_phase & 1U) == 0U) {
		heartbeat_led_set(false);
		s_msg_blink_phase++;
		if (s_msg_blink_phase >= s_msg_blink_count * 2U - 1U) {
			k_work_reschedule(&s_led_on_work, K_MSEC(LED_CYCLE_MS));
			return;
		}
	} else {
		heartbeat_led_set(true);
		s_msg_blink_phase++;
	}
	k_work_reschedule(&s_msg_blink_work, K_MSEC(LED_MSG_BLINK_MS));
}

static void tx_led_off_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	s_tx_led_active = false;
	heartbeat_led_set(false);
	if (!zephcore_leds_disabled()) {
		k_work_reschedule(&s_led_on_work, K_MSEC(LED_CYCLE_MS));
	}
}
#endif
#endif /* HAS_ANY_HEARTBEAT_LED */

/*
 * Weak: called after s_leds_disabled changes so each UI variant can sync its
 * own display state. Overridden by ui_task.c (button UI) to update
 * ui_state->leds_disabled so the LEDs page shows the correct toggle state.
 */
__attribute__((weak)) void ui_led_on_disabled_changed(bool disabled) { ARG_UNUSED(disabled); }

void ui_led_heartbeat_init(void)
{
#if HAS_XIAO_CHARGE_STATUS
	if (gpio_is_ready_dt(&s_charge_full_led)) {
		gpio_pin_configure_dt(&s_charge_full_led, GPIO_OUTPUT_INACTIVE);
		k_work_reschedule(&s_xiao_charge_work, K_NO_WAIT);
	}
#endif
#if HAS_ANY_HEARTBEAT_LED
	if (heartbeat_led_is_ready()) {
#if HAS_HEARTBEAT_LED
		gpio_pin_configure_dt(&s_heartbeat_led, GPIO_OUTPUT_INACTIVE);
#else
		heartbeat_led_write(false);
#endif
		k_work_init_delayable(&s_led_on_work, led_on_work_handler);
		k_work_init_delayable(&s_led_off_work, led_off_work_handler);
#if defined(CONFIG_BOARD_T1000_E)
		k_work_init_delayable(&s_msg_blink_work, msg_blink_work_handler);
		k_work_init_delayable(&s_tx_led_off_work, tx_led_off_work_handler);
#endif
		k_work_reschedule(&s_led_on_work, K_NO_WAIT);
		LOG_INF("LED heartbeat started");
	}
#if HAS_MSG_LED
	if (gpio_is_ready_dt(&s_msg_led)) {
		gpio_pin_configure_dt(&s_msg_led, GPIO_OUTPUT_INACTIVE);
		LOG_INF("msg LED ready");
	}
#endif
#if HAS_LOW_BATTERY_LED
	if (gpio_is_ready_dt(&s_low_battery_led)) {
		gpio_pin_configure_dt(&s_low_battery_led, GPIO_OUTPUT_INACTIVE);
		LOG_INF("low-battery LED ready");
	}
#endif
#if HAS_M3_CHARGE_STATUS
	if (gpio_is_ready_dt(&s_charge_usb_detect)) {
		gpio_pin_configure_dt(&s_charge_usb_detect, GPIO_INPUT);
	}
#endif
#if HAS_T1000_CHARGE_STATUS
	if (gpio_is_ready_dt(&s_charge_usb_detect)) {
		gpio_pin_configure_dt(&s_charge_usb_detect, GPIO_INPUT);
	}
#endif
#if HAS_BLE_STATUS_LED && !HEARTBEAT_IS_BLE_STATUS_LED
	if (gpio_is_ready_dt(&s_ble_status_led)) {
		gpio_pin_configure_dt(&s_ble_status_led, GPIO_OUTPUT_INACTIVE);
	}
#endif
#endif
}

void ui_set_heartbeat_led(bool enabled)
{
#if HAS_ANY_HEARTBEAT_LED
	if (enabled && !zephcore_leds_disabled()) {
		if (heartbeat_led_is_ready()) {
			k_work_reschedule(&s_led_on_work, K_NO_WAIT);
		}
	} else {
		k_work_cancel_delayable(&s_led_on_work);
		k_work_cancel_delayable(&s_led_off_work);
		heartbeat_led_write(false);
#if HAS_MSG_LED
		gpio_pin_set_dt(&s_msg_led, 0);
#endif
#if HAS_BLE_STATUS_LED
		gpio_pin_set_dt(&s_ble_status_led, 0);
#endif
	}
#else
	(void)enabled;
#endif
}

void ui_led_set_ble_connected(bool connected)
{
#if HAS_BLE_STATUS_LED
	s_ble_connected = connected;
	#if defined(CONFIG_BOARD_LILYGO_TECHO) && HAS_MSG_LED
	/* Apply the new colour immediately instead of waiting for the next
	 * five-second heartbeat cycle. */
	if (!zephcore_leds_disabled() && gpio_is_ready_dt(&s_heartbeat_led) &&
	    gpio_is_ready_dt(&s_msg_led) && gpio_is_ready_dt(&s_ble_status_led)) {
		k_work_cancel_delayable(&s_led_on_work);
		k_work_cancel_delayable(&s_led_off_work);
		heartbeat_sequence_reset();
		heartbeat_led_set(false);
		gpio_pin_set_dt(&s_msg_led, 0);
		gpio_pin_set_dt(&s_ble_status_led, 0);
		k_work_reschedule(&s_led_on_work, K_NO_WAIT);
	}
	#endif
#else
	ARG_UNUSED(connected);
#endif
}

void ui_led_set_ble_enabled(bool enabled)
{
#if HAS_BLE_STATUS_LED
	s_ble_enabled = enabled;
#else
	ARG_UNUSED(enabled);
#endif
}

/*
 * Strong override of the weak hook in led_gate.c: react to a gate change from
 * anywhere (UI toggle, "set leds", boot). Stops or restarts the heartbeat cycle
 * and refreshes the UI's LED page. The gate flag itself is already set by the
 * time we get here — do NOT call back into ui_set_leds_disabled() from here.
 */
void zephcore_leds_ui_sync(bool disabled)
{
#if HAS_ANY_HEARTBEAT_LED
	if (disabled) {
		k_work_cancel_delayable(&s_led_on_work);
		k_work_cancel_delayable(&s_led_off_work);
		heartbeat_led_write(false);
#if HAS_MSG_LED
		gpio_pin_set_dt(&s_msg_led, 0);
#endif
#if HAS_BLE_STATUS_LED
		gpio_pin_set_dt(&s_ble_status_led, 0);
#endif
	} else if (!k_work_delayable_is_pending(&s_led_on_work) &&
		   !k_work_delayable_is_pending(&s_led_off_work)) {
		/* Restart heartbeat only if it was stopped (avoids spurious pulse) */
		if (heartbeat_led_is_ready()) {
			k_work_reschedule(&s_led_on_work, K_NO_WAIT);
		}
	}
#else
	(void)disabled;
#endif
	ui_led_on_disabled_changed(disabled);
}

bool ui_leds_disabled(void)
{
	return zephcore_leds_disabled();
}

void ui_set_leds_disabled(bool disabled)
{
	zephcore_leds_set_disabled(disabled);
}

/* Start a short forced flash pattern on T-1000E. */
#if HAS_HEARTBEAT_LED && defined(CONFIG_BOARD_T1000_E)
static void t1000_led_flash_pattern(uint8_t count)
{
	if (zephcore_led_status_priority_active()) {
		return;
	}
	if (count == 0 || !gpio_is_ready_dt(&s_heartbeat_led)) {
		return;
	}
	k_work_cancel_delayable(&s_led_on_work);
	k_work_cancel_delayable(&s_led_off_work);
	k_work_cancel_delayable(&s_msg_blink_work);
	k_work_cancel_delayable(&s_tx_led_off_work);
	s_tx_led_active = false;
	s_msg_blink_count = count;
	s_msg_blink_phase = 0;
	heartbeat_led_set(true);
	k_work_reschedule(&s_msg_blink_work, K_MSEC(LED_MSG_BLINK_MS));
}
#endif

/* Flash the heartbeat LED on message receipt. T-1000E always uses three fast,
 * non-blocking flashes when no phone is connected, including with LEDs off;
 * other boards retain the existing single pulse and LEDs-off behaviour. */
void ui_led_flash_msg(void)
{
#if HAS_ANY_HEARTBEAT_LED
	if (zephcore_leds_hb_mode() == LEDS_HB_OFF ||
	    zephcore_leds_hb_mode() == LEDS_HB_HB ||
	    zephcore_led_status_priority_active()) {
		return;
	}
#if defined(CONFIG_BOARD_T1000_E)
	if (s_tx_led_active) return;
#endif
	if ((!zephcore_leds_disabled() || IS_ENABLED(CONFIG_BOARD_T1000_E)) &&
	    heartbeat_led_is_ready()) {
		k_work_cancel_delayable(&s_led_on_work);
		k_work_cancel_delayable(&s_led_off_work);
		heartbeat_sequence_reset();
#if defined(CONFIG_BOARD_T1000_E)
		t1000_led_flash_pattern(LED_MSG_BLINKS);
#else
		heartbeat_led_set(true);
		k_work_reschedule(&s_led_off_work, K_MSEC(LED_ON_MSG_MS));
#endif
	}
#endif
}

/* Confirm a local on/off state change: one short flash for on, two for off.
 * T-1000E performs this even when its heartbeat is disabled. */
void ui_led_confirm_state(bool enabled)
{
#if HAS_HEARTBEAT_LED && defined(CONFIG_BOARD_T1000_E)
	t1000_led_flash_pattern(enabled ? 1 : 2);
#else
	ARG_UNUSED(enabled);
#endif
}

/* Force the T-1000E LED on for two seconds after a user message or advert.
 * Other boards deliberately keep their existing LED behaviour. */
void ui_led_force_tx(void)
{
#if HAS_HEARTBEAT_LED && defined(CONFIG_BOARD_T1000_E)
	if (zephcore_led_status_priority_active()) {
		return;
	}
	if (gpio_is_ready_dt(&s_heartbeat_led)) {
		k_work_cancel_delayable(&s_led_on_work);
		k_work_cancel_delayable(&s_led_off_work);
		heartbeat_sequence_reset();
		k_work_cancel_delayable(&s_msg_blink_work);
		s_tx_led_active = true;
		heartbeat_led_set(true);
		k_work_reschedule(&s_led_on_work, K_MSEC(100));
		k_work_reschedule(&s_tx_led_off_work, K_SECONDS(2));
	}
#endif
}

/* Flash the heartbeat LED 3 times on shutdown.
 * Used as a visual power-off indicator when the buzzer is muted.
 * Suppressed by "set leds off" — a node the user asked to keep dark stays dark
 * even at power-off. */
void ui_led_flash_shutdown(void)
{
#if HAS_ANY_HEARTBEAT_LED
	if (!zephcore_leds_disabled() && heartbeat_led_is_ready()) {
		for (int i = 0; i < 3; i++) {
			heartbeat_led_write(true);
			k_sleep(K_MSEC(100));
			heartbeat_led_write(false);
			if (i < 2) {
				k_sleep(K_MSEC(100));
			}
		}
	}
#endif
}

/* ========== Battery refresh ==========
 * Lazy: render path calls ui_refresh_battery(); ADC only fires when the
 * cached reading is older than UI_BATT_REFRESH_MS. Telemetry / stats paths
 * read fresh directly via their own callbacks — this gate only governs the
 * local display. */
#define UI_BATT_REFRESH_MS  30000

/* The board's own percentage: its discharge curve, or the fuel gauge. */
static uint8_t (*s_batt_pct_provider)(void);
static uint32_t s_batt_last_read_ms;
static bool s_batt_ever_read;

void ui_set_battery_provider(uint16_t (*mv)(void), uint8_t (*pct)(void))
{
	s_batt_provider = mv;
	s_batt_pct_provider = pct;
}

void ui_set_power_source_provider(bool (*provider)(void))
{
	s_power_source_provider = provider;
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
	uint16_t mv = s_batt_provider();
	ui_set_battery(mv, (mv && s_batt_pct_provider) ? s_batt_pct_provider() : 0);
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

/* ========== Power-off: the UI's part ==========
 * zephcore_power_off() (adapters/board/zephyr_poweroff.c) runs this first,
 * then powers the board down. */
void ui_before_power_off(void)
{
	/* Stop heartbeat LED cycle (cancels both works). */
	ui_set_heartbeat_led(false);

	/* Display off — content stays visible on EPD, blanks on OLED. */
#ifdef CONFIG_ZEPHCORE_UI_DISPLAY
	mc_display_off();
#endif
}

/* ========== Low-battery warning screen ==========
 * The policy itself is app/PowerPolicy.cpp. */
__attribute__((weak)) bool ui_shutdown_in_progress(void) { return false; }
__attribute__((weak)) bool ui_request_manual_shutdown(void) { return false; }

#ifdef CONFIG_ZEPHCORE_UI_DISPLAY
void ui_show_low_battery(bool hold)
{
	/* Wake the panel (OLED may be blanked by auto-off; EPD is always
	 * visible). Centre two lines; the message persists on e-paper after
	 * power is cut, so e-paper needs no hold delay. */
	mc_display_on();
	mc_display_clear();

	const char *l1 = "Low Battery";
	const char *l2 = "Shutting Down";
	uint16_t w  = mc_display_width();
	uint8_t  fw = mc_display_font_width();
	uint8_t  fh = mc_display_font_height();
	int x1 = (fw && w) ? ((int)w - (int)strlen(l1) * fw) / 2 : 0;
	int x2 = (fw && w) ? ((int)w - (int)strlen(l2) * fw) / 2 : 0;
	if (x1 < 0) x1 = 0;
	if (x2 < 0) x2 = 0;
	int y1 = (int)mc_display_height() / 2 - (int)fh;
	if (y1 < 0) y1 = 0;

	mc_display_text(x1, y1, l1, false);
	mc_display_text(x2, y1 + fh + 2, l2, false);
	mc_display_finalize();
	/* The normal EPD auto-off path leaves SSD16xx blanked.  finalize() writes
	 * its RAM but does not move pixels until blanking is released, so commit
	 * this terminal frame explicitly before System OFF. */
	mc_display_epd_commit();

	/* OLED blanks the instant power drops, so hold long enough to read it.
	 * EPD keeps the image with no power, so skip the delay. The deferred-
	 * poweroff (grace) path passes hold=false: it must NOT block the main
	 * thread, because that thread has to service the app's message fetch
	 * during the grace window — the grace timer provides the on-screen dwell
	 * instead. */
	if (hold && !mc_display_is_epd()) {
		k_sleep(K_MSEC(3000));
	}
}
#else
void ui_show_low_battery(bool hold)
{
	ARG_UNUSED(hold);
}
#endif

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
