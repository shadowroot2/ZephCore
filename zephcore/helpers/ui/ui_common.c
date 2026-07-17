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

#include <ZephyrSensorManager.h>   /* gps_power_off_for_shutdown */
#include "ui_mesh_actions.h"        /* mesh_disable_power_regulators (weak) */
#include <helpers/battery_curve.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <string.h>

#ifdef CONFIG_POWEROFF
#include <zephyr/sys/poweroff.h>
#endif

#if defined(CONFIG_SOC_FAMILY_NORDIC_NRF)
#include <hal/nrf_gpio.h>
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ui_led, CONFIG_ZEPHCORE_BOARD_LOG_LEVEL);

static uint16_t (*s_batt_provider)(void);

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
		ui_set_buzzer_quiet(buzzer_is_quiet());
	}
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
#define LED_LOW_BATT_THRESHOLD_PCT     25
#define LED_LOW_BATT_BLINK_MS           80
#define LED_LOW_BATT_BLINKS              3
#if defined(CONFIG_BOARD_T1000_E)
#define LED_MSG_BLINK_MS   80  /* Fast incoming-message flash phase */
#define LED_MSG_BLINKS      3  /* Number of flashes for one incoming message */
#endif

#if HAS_HEARTBEAT_LED
static struct k_work_delayable s_led_on_work;
static struct k_work_delayable s_led_off_work;
static uint8_t s_heartbeat_blinks_left;
static uint16_t s_heartbeat_on_ms;
static bool s_heartbeat_low_batt_cycle;
#if defined(CONFIG_BOARD_T1000_E)
static struct k_work_delayable s_msg_blink_work;
static struct k_work_delayable s_tx_led_off_work;
static uint8_t s_msg_blink_phase;
static uint8_t s_msg_blink_count;
static bool s_tx_led_active;
#endif
static bool s_leds_disabled;
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

/* The heartbeat is not a precise fuel gauge; it only needs a stable visual
 * low-charge indication. Refresh its voltage estimate at the same cadence as
 * the UI, rather than waking the ADC on every LED pulse. */
static bool heartbeat_low_battery(void)
{
	static uint32_t last_sample_ms;
	static bool sampled;
	static bool low;
	uint32_t now = k_uptime_get_32();

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
		ui_set_buzzer_quiet(buzzer_is_quiet());
#endif
		last_sample_ms = now;
		sampled = true;
	}
	return low;
}

static void heartbeat_sequence_reset(void)
{
	s_heartbeat_blinks_left = 0;
	s_heartbeat_on_ms = 0;
	s_heartbeat_low_batt_cycle = false;
}

static void heartbeat_led_set(bool on)
{
#if HAS_LED_ENABLE
	if (on) {
		gpio_pin_set_dt(&s_led_enable, 1);
	}
#endif
	gpio_pin_set_dt(&s_heartbeat_led, on ? 1 : 0);
#if HAS_LED_ENABLE
	if (!on) {
		gpio_pin_set_dt(&s_led_enable, 0);
	}
#endif
}

static void led_off_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	heartbeat_led_set(false);
#if HAS_MSG_LED
	gpio_pin_set_dt(&s_msg_led, 0);
#endif
#if HAS_BLE_STATUS_LED
	gpio_pin_set_dt(&s_ble_status_led, 0);
#endif

	if (s_heartbeat_blinks_left > 0) {
		s_heartbeat_blinks_left--;
	}
	if (s_heartbeat_blinks_left > 0) {
		/* Continue the low-battery burst after a short dark gap. */
		k_work_reschedule(&s_led_on_work, K_MSEC(LED_LOW_BATT_BLINK_MS));
		return;
	}

	uint32_t delay_ms;
	if (s_heartbeat_low_batt_cycle) {
		/* Three 80 ms flashes, separated by two 80 ms gaps, start once
		 * every five seconds. */
		delay_ms = LED_CYCLE_MS -
			(LED_LOW_BATT_BLINKS * LED_LOW_BATT_BLINK_MS +
			 (LED_LOW_BATT_BLINKS - 1U) * LED_LOW_BATT_BLINK_MS);
	} else {
		delay_ms = LED_CYCLE_MS - s_heartbeat_on_ms;
	}
	heartbeat_sequence_reset();
	k_work_reschedule(&s_led_on_work, K_MSEC(delay_ms));
}

static void led_on_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	uint16_t mc = ui_led_get_msg_count();
	if (s_heartbeat_blinks_left == 0) {
		s_heartbeat_low_batt_cycle = heartbeat_low_battery();
		s_heartbeat_blinks_left = s_heartbeat_low_batt_cycle ?
			LED_LOW_BATT_BLINKS : 1;
		s_heartbeat_on_ms = s_heartbeat_low_batt_cycle ?
			LED_LOW_BATT_BLINK_MS : (mc > 0 ? LED_ON_MSG_MS : LED_ON_MS);
	}

	if (!s_leds_disabled) {
		#if HAS_BLE_STATUS_LED
		bool ble_waiting = s_ble_enabled && !s_ble_connected;
		#endif
		#if !HEARTBEAT_IS_BLE_STATUS_LED
		heartbeat_led_set(true);
		#else
		if (ble_waiting) {
			heartbeat_led_set(true);
		}
		#endif
#if HAS_MSG_LED
		if (mc > 0) {
			gpio_pin_set_dt(&s_msg_led, 1);
		}
#endif
		#if HAS_BLE_STATUS_LED && !HEARTBEAT_IS_BLE_STATUS_LED
		if (ble_waiting) {
			gpio_pin_set_dt(&s_ble_status_led, 1);
		}
		#endif
	}
	k_work_reschedule(&s_led_off_work, K_MSEC(s_heartbeat_on_ms));
}

/* Complete a non-blocking LED flash sequence. Keeping this in the
 * work queue avoids delaying LoRa/BLE processing, unlike a k_sleep loop. */
#if defined(CONFIG_BOARD_T1000_E)
static void msg_blink_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if ((s_msg_blink_phase & 1U) == 0U) {
		/* End one of the requested ON phases. */
		heartbeat_led_set(false);
		s_msg_blink_phase++;
		if (s_msg_blink_phase >= s_msg_blink_count * 2U - 1U) {
			/* The requested flash sequence is done; resume heartbeat later. */
			k_work_reschedule(&s_led_on_work, K_MSEC(LED_CYCLE_MS));
			return;
		}
	} else {
		/* Start the next flash after the short OFF gap. */
		heartbeat_led_set(true);
		s_msg_blink_phase++;
	}

	k_work_reschedule(&s_msg_blink_work, K_MSEC(LED_MSG_BLINK_MS));
}

/* A user-originated message or advert gets a solid two-second TX signal.
 * It intentionally ignores the persisted LEDs-off preference and BLE state. */
static void tx_led_off_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	s_tx_led_active = false;
	heartbeat_led_set(false);
	if (!s_leds_disabled) {
		k_work_reschedule(&s_led_on_work, K_MSEC(LED_CYCLE_MS));
	}
}
#endif
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
#if HAS_LED_ENABLE
		if (gpio_is_ready_dt(&s_led_enable)) {
			gpio_pin_configure_dt(&s_led_enable, GPIO_OUTPUT_INACTIVE);
		}
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
#if HAS_BLE_STATUS_LED && !HEARTBEAT_IS_BLE_STATUS_LED
	if (gpio_is_ready_dt(&s_ble_status_led)) {
		gpio_pin_configure_dt(&s_ble_status_led, GPIO_OUTPUT_INACTIVE);
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
		heartbeat_sequence_reset();
#if defined(CONFIG_BOARD_T1000_E)
		k_work_cancel_delayable(&s_msg_blink_work);
		k_work_cancel_delayable(&s_tx_led_off_work);
		s_tx_led_active = false;
#endif
		heartbeat_led_set(false);
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

void ui_set_leds_disabled(bool disabled)
{
#if HAS_HEARTBEAT_LED
	s_leds_disabled = disabled;
	if (disabled) {
		k_work_cancel_delayable(&s_led_on_work);
		k_work_cancel_delayable(&s_led_off_work);
		heartbeat_sequence_reset();
#if defined(CONFIG_BOARD_T1000_E)
		k_work_cancel_delayable(&s_msg_blink_work);
#endif
		/* A TX indication remains visible for its complete two seconds even
		 * if the user switches LEDs off while it is in progress. */
#if !defined(CONFIG_BOARD_T1000_E)
		heartbeat_led_set(false);
#else
		if (!s_tx_led_active) {
			heartbeat_led_set(false);
		}
#endif
#if HAS_MSG_LED
		gpio_pin_set_dt(&s_msg_led, 0);
#endif
#if HAS_BLE_STATUS_LED
		gpio_pin_set_dt(&s_ble_status_led, 0);
#endif
	} else if (!k_work_delayable_is_pending(&s_led_on_work) &&
		   !k_work_delayable_is_pending(&s_led_off_work)) {
		/* Restart heartbeat only if it was stopped (avoids spurious pulse) */
	#if !defined(CONFIG_BOARD_T1000_E)
		if (gpio_is_ready_dt(&s_heartbeat_led)) {
			k_work_reschedule(&s_led_on_work, K_NO_WAIT);
		}
	#else
		if (!s_tx_led_active && gpio_is_ready_dt(&s_heartbeat_led)) {
			k_work_reschedule(&s_led_on_work, K_NO_WAIT);
		}
	#endif
	}
#else
	(void)disabled;
#endif
	ui_led_on_disabled_changed(disabled);
}

/* Start a short forced flash pattern on T-1000E. */
#if HAS_HEARTBEAT_LED && defined(CONFIG_BOARD_T1000_E)
static void t1000_led_flash_pattern(uint8_t count)
{
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
#if HAS_HEARTBEAT_LED
	#if defined(CONFIG_BOARD_T1000_E)
	if (s_tx_led_active) {
		return;
	}
	#endif
	#if defined(CONFIG_BOARD_T1000_E)
	if (gpio_is_ready_dt(&s_heartbeat_led)) {
	#else
	if (!s_leds_disabled && gpio_is_ready_dt(&s_heartbeat_led)) {
	#endif
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
	if (gpio_is_ready_dt(&s_heartbeat_led)) {
		k_work_cancel_delayable(&s_led_on_work);
		k_work_cancel_delayable(&s_led_off_work);
		heartbeat_sequence_reset();
		k_work_cancel_delayable(&s_msg_blink_work);
		s_tx_led_active = true;
		heartbeat_led_set(true);
		k_work_reschedule(&s_tx_led_off_work, K_SECONDS(2));
	}
#endif
}

/* Flash the heartbeat LED 3 times on shutdown.
 * Used as a visual power-off indicator when the buzzer is muted. */
void ui_led_flash_shutdown(void)
{
#if HAS_HEARTBEAT_LED
	if (gpio_is_ready_dt(&s_heartbeat_led)) {
		for (int i = 0; i < 3; i++) {
			heartbeat_led_set(true);
			k_sleep(K_MSEC(100));
			heartbeat_led_set(false);
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

static uint32_t s_batt_last_read_ms;
static bool s_batt_ever_read;
static bool (*s_power_source_provider)(void);

void ui_set_battery_provider(uint16_t (*provider)(void))
{
	s_batt_provider = provider;
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

/* ========== System OFF preparation ==========
 * Shared by both UI variants. Caller is responsible for any shutdown chime
 * and the final sys_poweroff() call; this just leaves the SoC + peripherals
 * in the lowest-power state with a wakeup source armed.
 *
 * On nRF52 the SoC enters System OFF (~1 µA) but GPIO output latches and
 * SENSE bits persist across the transition — so we must explicitly:
 *   - hold every power-enable GPIO LOW so external chips don't keep drawing
 *   - hold the LoRa radio in HW reset (its internal duty cycle would
 *     otherwise keep cycling autonomously, drawing mA)
 *   - configure SENSE on sw0 so a button press wakes the chip (the nRF GPIO
 *     driver doesn't honour the DT wakeup-source property, so the dtsi
 *     marker is inert without this)
 *
 * Non-nRF platforms skip the SENSE block; they rely on Zephyr's wakeup-source
 * DT property which is honoured by their respective GPIO drivers.
 */
void ui_prepare_for_system_off(void)
{
	/* 1. Stop heartbeat LED cycle (cancels both works). */
	ui_set_heartbeat_led(false);

	/* 2. Display off — content stays visible on EPD, blanks on OLED. */
#ifdef CONFIG_ZEPHCORE_UI_DISPLAY
	mc_display_off();
#endif

	/* 3. Drive power-enable GPIOs LOW so peripherals don't keep drawing.
	 * Do NOT touch BLE here — that corrupts controller state and prevents
	 * clean reboot on wake. */
	gps_power_off_for_shutdown();
	mesh_disable_power_regulators();   /* weak-stubbed on non-companion roles */

	/* 4. Hold LoRa radio in HW reset.
	 * SX126x/LR11xx duty-cycle mode would otherwise keep the radio cycling
	 * autonomously (mA) while the SoC is in System OFF.  nRF52 output
	 * latches persist across System OFF → chip stays in reset (~0 µA). */
#if DT_NODE_EXISTS(DT_ALIAS(lora0)) && DT_NODE_HAS_PROP(DT_ALIAS(lora0), reset_gpios)
	{
		static const struct gpio_dt_spec lora_reset =
			GPIO_DT_SPEC_GET(DT_ALIAS(lora0), reset_gpios);
		gpio_pin_configure_dt(&lora_reset, GPIO_OUTPUT_ACTIVE);
	}
#endif

	/* 5. Configure GPIO SENSE for sw0 button wakeup, after waiting for the
	 * user to release any held button (otherwise DETECT is already asserted
	 * when we enter System OFF and the chip never sleeps cleanly).
	 * nRF only — other platforms rely on the DT wakeup-source property. */
#if defined(CONFIG_SOC_FAMILY_NORDIC_NRF) && DT_NODE_EXISTS(DT_ALIAS(sw0))
	{
#define _SW0_NODE  DT_ALIAS(sw0)
#define _SW0_PORT  DT_PROP(DT_GPIO_CTLR(_SW0_NODE, gpios), port)
#define _SW0_PIN   DT_GPIO_PIN(_SW0_NODE, gpios)
#define _SW0_FLAGS DT_GPIO_FLAGS(_SW0_NODE, gpios)

		static const struct gpio_dt_spec sw0 =
			GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
		gpio_pin_configure_dt(&sw0, GPIO_INPUT);

		int64_t deadline = k_uptime_get() + 5000;
		while (gpio_pin_get_dt(&sw0) && k_uptime_get() < deadline) {
			k_sleep(K_MSEC(10));
		}

		nrf_gpio_cfg_sense_input(
			NRF_GPIO_PIN_MAP(_SW0_PORT, _SW0_PIN),
			(_SW0_FLAGS & GPIO_PULL_UP)   ? NRF_GPIO_PIN_PULLUP   :
			(_SW0_FLAGS & GPIO_PULL_DOWN) ? NRF_GPIO_PIN_PULLDOWN :
						       NRF_GPIO_PIN_NOPULL,
			(_SW0_FLAGS & GPIO_ACTIVE_LOW) ? NRF_GPIO_PIN_SENSE_LOW
						       : NRF_GPIO_PIN_SENSE_HIGH);
#undef _SW0_NODE
#undef _SW0_PORT
#undef _SW0_PIN
#undef _SW0_FLAGS
	}
#endif /* CONFIG_SOC_FAMILY_NORDIC_NRF && sw0 */
}

/* ========== Low-battery auto-shutdown ==========
 * Companion only. Driven off the existing housekeeping tick — self-throttled,
 * so there is no dedicated poll. Disabled entirely (compiled out) unless a
 * board sets CONFIG_ZEPHCORE_AUTO_SHUTDOWN_MILLIVOLTS > 0. */
#if defined(CONFIG_ZEPHCORE_AUTO_SHUTDOWN_MILLIVOLTS) && \
	CONFIG_ZEPHCORE_AUTO_SHUTDOWN_MILLIVOLTS > 0

/* How often we actually sample the ADC for the shutdown check. The caller
 * fires every housekeeping tick (~5 s); this gate keeps the divider from
 * being energised more than necessary while still catching a sagging cell
 * well before it collapses. */
#define UI_AUTO_SHUTDOWN_CHECK_MS  30000

/* Consecutive below-threshold readings required before shutdown.
 * 3 hits × 30 s = 90 s confirm window — a single TX-induced sag that
 * lands on a check window won't trigger a false shutdown. */
#define UI_AUTO_SHUTDOWN_CONFIRM_COUNT  3

/* Runtime threshold (mV); 0 disables. Seeded from the Kconfig default, then
 * overridden at boot from prefs and live via the CLI (ui_set_auto_shutdown_mv). */
static uint16_t s_auto_shutdown_mv = CONFIG_ZEPHCORE_AUTO_SHUTDOWN_MILLIVOLTS;
static uint8_t  s_low_count;

void ui_set_auto_shutdown_mv(uint16_t mv)
{
	s_auto_shutdown_mv = mv;
}

/* Pre-shutdown hook + deferred power-off. When the hook queues a shutdown
 * message, power-off is deferred on a work item so the main loop keeps
 * running and can deliver it. */
static ui_shutdown_fn s_shutdown_hook;
static bool s_shutting_down;

static void shutdown_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
#ifdef CONFIG_POWEROFF
	ui_prepare_for_system_off();
	sys_poweroff();
	CODE_UNREACHABLE;
#endif
}
static K_WORK_DELAYABLE_DEFINE(s_shutdown_work, shutdown_work_fn);

void ui_set_shutdown_hook(ui_shutdown_fn fn)
{
	s_shutdown_hook = fn;
}

#ifdef CONFIG_ZEPHCORE_UI_DISPLAY
static void auto_shutdown_warn_screen(bool hold)
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
#endif /* CONFIG_ZEPHCORE_UI_DISPLAY */

void ui_auto_shutdown_check(void)
{
	if (s_shutting_down) {
		return;  /* power-off already committed (deferred grace running) */
	}
	if (!s_batt_provider || s_auto_shutdown_mv == 0) {
		return;  /* no battery provider, or runtime-disabled */
	}

	uint32_t now = k_uptime_get_32();
	static uint32_t next_check_ms;   /* 0 at boot → first tick samples */
	if (next_check_ms != 0 && (now - next_check_ms) < UI_AUTO_SHUTDOWN_CHECK_MS) {
		return;
	}
	next_check_ms = now;

	uint16_t mv = s_batt_provider();
	if (mv == 0 || mv >= s_auto_shutdown_mv) {
		s_low_count = 0;
		return;  /* no battery hardware / reading, or healthy */
	}

	/* Don't power off while charging or USB-powered — the reading is the
	 * cell, not the supply, and yanking power on a bench cable is annoying. */
	if (s_power_source_provider && s_power_source_provider()) {
		LOG_INF("auto-shutdown: %u mV below threshold but externally powered", mv);
		s_low_count = 0;
		return;
	}

	s_low_count++;
	LOG_WRN("auto-shutdown: battery %u mV < %u mV (%u/%u)",
		mv, s_auto_shutdown_mv, s_low_count, UI_AUTO_SHUTDOWN_CONFIRM_COUNT);
	if (s_low_count < UI_AUTO_SHUTDOWN_CONFIRM_COUNT) {
		return;
	}

	LOG_WRN("auto-shutdown: confirmed — powering off");

	/* Let the app layer report this automatic shutdown. A queued message asks
	 * for a grace period while the main loop delivers it; otherwise power off
	 * immediately. */
	bool grace = s_shutdown_hook ? s_shutdown_hook(UI_SHUTDOWN_LOW_BATTERY, mv, now)
				     : false;
	s_shutting_down = true;

#ifdef CONFIG_POWEROFF
	if (grace) {
#ifdef CONFIG_ZEPHCORE_UI_DISPLAY
		auto_shutdown_warn_screen(false);  /* draw, don't block the loop */
#endif
		k_work_schedule(&s_shutdown_work, K_MSEC(UI_SHUTDOWN_GRACE_MS));
		return;  /* main loop keeps running → delivers the notice */
	}

#ifdef CONFIG_ZEPHCORE_UI_DISPLAY
	auto_shutdown_warn_screen(true);  /* nothing to deliver — 3 s OLED hold */
#endif
	ui_prepare_for_system_off();
	sys_poweroff();
	CODE_UNREACHABLE;
#else
	(void)grace;
#ifdef CONFIG_ZEPHCORE_UI_DISPLAY
	auto_shutdown_warn_screen(true);
#endif
	LOG_WRN("auto-shutdown: CONFIG_POWEROFF not enabled — cannot power off");
#endif
}

#else  /* feature disabled (non-nRF52 / threshold default 0) */

void ui_set_auto_shutdown_mv(uint16_t mv) { (void)mv; }
void ui_auto_shutdown_check(void) { }
void ui_set_shutdown_hook(ui_shutdown_fn fn) { (void)fn; }

#endif /* CONFIG_ZEPHCORE_AUTO_SHUTDOWN_MILLIVOLTS > 0 */

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
