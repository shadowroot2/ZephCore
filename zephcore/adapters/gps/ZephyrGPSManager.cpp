/*
 * SPDX-License-Identifier: MIT
 * Zephyr GPS Manager - GNSS power management, fix acquisition, constellation config
 *
 * Event-driven state machine with no polling loops:
 * - LoRa/BLE events trigger GPS enable/disable
 * - k_work_delayable handles standby/timeout timers
 * - GNSS callback fires on fix data from driver
 *
 * Power strategy:
 * - Direct GPIO toggle via gps-enable alias (all boards)
 * - T1000-E warm standby: VRTC stays powered during standby, preserving
 *   ephemeris/almanac/RTC in backup RAM for fast re-acquisition (3-8s vs 15-45s)
 * - GNSS UARTE suspended (device PM) while GPS is off/standby — releases
 *   HFCLK on nRF52840 (~0.5-1 mA), resumed before every wake
 * - Full power-off only on user-disable or System OFF
 */

#include "ZephyrGPSManager.h"
#include "../../helpers/pm_sleep_guard.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/pm/device.h>
#include <string.h>
#include <stdio.h>
#if defined(CONFIG_SOC_NRF52840)
#include <nrfx.h>
#endif

LOG_MODULE_REGISTER(zephcore_gps, CONFIG_ZEPHCORE_GPS_LOG_LEVEL);

/* ========== GNSS Support ========== */
#if DT_HAS_COMPAT_STATUS_OKAY(gnss_nmea_generic) || \
    DT_HAS_COMPAT_STATUS_OKAY(u_blox_m8) || \
    DT_HAS_COMPAT_STATUS_OKAY(u_blox_f9p) || \
    DT_HAS_COMPAT_STATUS_OKAY(quectel_lcx6g) || \
    DT_HAS_COMPAT_STATUS_OKAY(quectel_lc76g) || \
    DT_HAS_COMPAT_STATUS_OKAY(luatos_air530z)
#define HAS_GNSS 1
#include <zephyr/drivers/gnss.h>
#else
#define HAS_GNSS 0
#endif

/* ========== GPS Feature Detection ==========
 * Every HAS_GPS_* predicate is defined HERE, before first use. They are pure
 * devicetree tests with no side effects, kept apart from the variables they
 * gate so that ordering can never drift again.
 *
 * Why this block exists: HAS_GPS_UART used to be defined ~500 lines below its
 * first `#if`, and an undefined identifier in `#if` is silently 0 — so the
 * entire PMTK/UBX module-configuration path compiled to nothing on every
 * board, and GPS ran at module defaults (GPS-only constellations, no AOP).
 * If you add another HAS_GPS_* macro, define it in this block.
 */

/* GNSS module hangs off a UART we can write to (any compatible). */
#if HAS_GNSS && DT_NODE_HAS_STATUS(DT_NODELABEL(gnss), okay) && \
    DT_NODE_HAS_STATUS(DT_BUS(DT_NODELABEL(gnss)), okay)
#define HAS_GPS_UART 1
#else
#define HAS_GPS_UART 0
#endif

/* Discrete GPS power-enable GPIO (gps-enable alias). */
#if DT_NODE_EXISTS(DT_ALIAS(gps_enable))
#define HAS_GPS_POWER_CONTROL 1
#else
#define HAS_GPS_POWER_CONTROL 0
#endif

/* GPS powered from a PMU regulator rail (chosen zephcore,gps-power). */
#if DT_NODE_EXISTS(DT_CHOSEN(zephcore_gps_power))
#define HAS_GPS_POWER_REGULATOR 1
#else
#define HAS_GPS_POWER_REGULATOR 0
#endif

/* Runtime diagnostics toggle. Declared OUT here, not inside the HAS_GNSS
 * block: gps_set_diag()/gps_get_diag()/gps_get_diag_report() are part of the
 * unconditional public API and are compiled on boards with no GNSS at all,
 * so a declaration hidden behind HAS_GNSS breaks every such board. */
static bool gps_diag_on = false;

/* ========== GPS Power Strategy ==========
 * Module power is GPIO/regulator controlled — GNSS driver PM is not used
 * for the module itself:
 * - Wio Tracker L1 (L76K): P1.09 is the module's WAKEUP pin, not a supply
 *   switch. Per the L76K hardware design: WAKEUP is a digital input, active
 *   low with an internal pull-up, that "enters or exits Standby mode". In
 *   Standby the RF is powered off but the internal core and I/O power domain
 *   stay active, so VCC is never removed and ephemeris/almanac/RTC survive —
 *   every wake is a warm start, not a cold one. (Backup mode, the deeper
 *   state, requires cutting VCC while V_BCKP holds the RTC domain; this
 *   board has no VCC switch, so Standby is the floor available to us.)
 * - T1000-E (AG3335): GPS_EN LOW + VRTC HIGH = warm standby (ephemeris
 *   preserved via backup RAM, ~1-2µA VRTC current)
 * - All boards: gps-enable alias → GPIO power control
 *
 * CONFIG_PM_DEVICE is on globally, but nothing suspends automatically —
 * system-managed suspend is compiled only under CONFIG_PM (off everywhere).
 * This manager makes exactly two kinds of PM calls, both main-thread only:
 * - a one-time RESUME of the GNSS device at boot (gnss-nmea-generic inits
 *   suspended under CONFIG_PM_DEVICE and never opens its pipe otherwise);
 * - suspend/resume of the GNSS UARTE around standby/off (an armed UARTE RX
 *   holds HFCLK ≈0.5-1 mA on nRF52840 even with the module powered off).
 * The old "PM broke GPS" deadlock was modem_chat_run_script() being reached
 * from the system workqueue via driver PM hooks — the air530z driver is
 * PM-less now and every PM call here stays on the main thread. */

/* ========== GPS State - Power Management ========== */
#if HAS_GNSS
static struct gps_position current_pos;
static struct gnss_time current_utc;
static bool gps_enabled = false;
static bool gps_available = false;
static K_MUTEX_DEFINE(gps_mutex);
static gps_enable_callback_t gps_enable_cb = NULL;
static gps_fix_callback_t gps_fix_cb = NULL;
static gps_event_callback_t gps_event_cb = NULL;

/* Pending GPS actions — set by work handlers (system work queue),
 * consumed by gps_process_event() (main thread).
 * This avoids calling blocking GNSS APIs from the system work queue,
 * which deadlocks because modem_chat_run_script() blocks on a semaphore
 * that's signaled from the same work queue. */
#define GPS_ACTION_WAKE     BIT(0)  /* Wake from standby → start acquiring */
#define GPS_ACTION_TIMEOUT  BIT(1)  /* Acquisition timeout → go to standby */
#define GPS_ACTION_FIX_DONE BIT(2)  /* Got enough good fixes → go to standby */
static atomic_t pending_gps_actions;

/* GPS Power Management State Machine */
enum gps_state {
	GPS_STATE_OFF,          /* GPS disabled by user */
	GPS_STATE_STANDBY,      /* GPS enabled but sleeping (5 min cycle) */
	GPS_STATE_ACQUIRING,    /* GPS awake, waiting for fixes */
};

static enum gps_state gps_current_state = GPS_STATE_OFF;
static uint8_t consecutive_good_fixes = 0;
static bool first_fix_acquired = false;  /* True after first 3-good-fix cycle since enable. Cleared on gps_enable(false) and at boot. */
static bool first_acquire_used = false;  /* True once the one-time long cold-start window has ended (fix or timeout). Cleared on gps_enable(false) and at boot. */
static bool gps_time_synced = false;     /* True after GPS syncs RTC. Starts false at boot (RTC reset),
                                          * set true after 3 good fixes, cleared when GPS disabled. */
static int64_t last_fix_uptime_ms = 0;  /* k_uptime when last validated fix was acquired */
static int64_t standby_start_ms = 0;    /* k_uptime when standby started (for next-wake calc) */
static uint64_t standby_interval_ms = 0; /* How long standby lasts (for next-wake calc) */
static uint16_t last_gnss_satellites = 0; /* Updated on every active GNSS callback, even before fix */
static uint16_t last_gnss_visible_satellites = 0; /* Updated from GSV when GNSS_SATELLITES is enabled */
#if IS_ENABLED(CONFIG_GNSS_SATELLITES)
/* GNSS system bits currently span bit 0 (GPS) through bit 13 (QZSS L1S).
 * GSV is published separately for each constellation, so retain the most
 * recent count for every system and present their sum to the UI. */
#define GNSS_VISIBLE_SYSTEM_COUNT 14
static uint16_t gnss_visible_by_system[GNSS_VISIBLE_SYSTEM_COUNT];
#endif

#define GPS_GOOD_FIX_COUNT       3       /* Need 3 consecutive good fixes */
#define GPS_MIN_SATELLITES       4       /* Minimum satellites for valid fix */

/* Runtime-configurable intervals, initialized from Kconfig defaults */
static uint32_t gps_acquire_timeout_ms   = CONFIG_ZEPHCORE_GPS_FIX_TIMEOUT_SEC * 1000U;
static uint32_t gps_first_fix_timeout_ms = CONFIG_ZEPHCORE_GPS_FIRST_FIX_TIMEOUT_SEC * 1000U;
static uint32_t gps_wake_interval_ms     = CONFIG_ZEPHCORE_GPS_POLL_INTERVAL_SEC * 1000U;

/* Duty cycle vs always-on: a non-zero standby interval duty-cycles; interval 0
 * keeps the GPS in continuous acquisition (never sleeps) so it streams fresh
 * fixes for telemetry and can download a full almanac. */
static inline bool gps_duty_cycling(void)
{
	return gps_wake_interval_ms != 0;
}

/* k_uptime of the last always-on fix-callback invocation — rate-limits the
 * RTC sync / node-position update to gps_acquire_timeout_ms while streaming
 * (see gnss_data_cb), so 1Hz fixes don't fire the callback continuously. */
static int64_t last_promote_ms = 0;

/* Repeater acquire window — GPS only for time sync. The standby interval is
 * unified with companions via gps_wake_interval_ms (prefs.gps_interval). */
#define GPS_REPEATER_SYNC_TIMEOUT_MS   (5 * 60 * 1000)           /* 5 minutes */

static bool gps_repeater_mode = false;  /* True = repeater (time sync only), False = companion */
static bool gnss_activity_seen_this_cycle = false;  /* Runtime-only: set by GNSS callback while acquiring */

/* Forward declarations for work handlers and state functions */
static void gps_wake_work_fn(struct k_work *work);
static void gps_timeout_work_fn(struct k_work *work);
static void gps_go_to_standby(void);
static void gps_start_acquiring(void);

/* Delayable work for event-driven timers (no polling!) */
static K_WORK_DELAYABLE_DEFINE(gps_wake_work, gps_wake_work_fn);
static K_WORK_DELAYABLE_DEFINE(gps_timeout_work, gps_timeout_work_fn);

#else
static gps_enable_callback_t gps_enable_cb = NULL;
#endif

void gps_set_enable_callback(gps_enable_callback_t cb)
{
	gps_enable_cb = cb;
}

void gps_set_fix_callback(gps_fix_callback_t cb)
{
#if HAS_GNSS
	gps_fix_cb = cb;
#else
	ARG_UNUSED(cb);
#endif
}

void gps_set_event_callback(gps_event_callback_t cb)
{
#if HAS_GNSS
	gps_event_cb = cb;
#else
	ARG_UNUSED(cb);
#endif
}

#if HAS_GNSS

/* GNSS callback - called when new fix data is available */
static void gnss_data_cb(const struct device *dev, const struct gnss_data *data)
{
	ARG_UNUSED(dev);

	if (!gps_enabled || gps_current_state == GPS_STATE_STANDBY) {
		/* GPS disabled or in standby — ignore NMEA data.
		 * The GNSS driver fires callbacks as long as the UART has data,
		 * even after we drive GPS_EN LOW (module drains its buffer).
		 * On boards without GPS power control (e.g. RAK3401 where 3V3_S
		 * rail is shared with LoRa FEM), the GPS module stays powered in
		 * standby and keeps streaming NMEA — suppress those callbacks to
		 * avoid log spam and wasted CPU for the entire standby period. */
		return;
	}

	if (gps_current_state == GPS_STATE_ACQUIRING) {
		/* Any callback means GNSS hardware/UART path is alive, even without a fix. */
		gnss_activity_seen_this_cycle = true;
	}

	LOG_DBG("GNSS callback: fix=%d sats=%d state=%d",
		data->info.fix_status, data->info.satellites_cnt, gps_current_state);

	k_mutex_lock(&gps_mutex, K_FOREVER);
	last_gnss_satellites = data->info.satellites_cnt;

	if (data->info.fix_status >= GNSS_FIX_STATUS_GNSS_FIX) {
		/* Reject "Null Island" (0,0) fixes. Zephyr's NMEA parser splits
		 * data across callbacks: gnss_nmea0183_parse_gga fills altitude +
		 * fix_status but NOT lat/lon; gnss_nmea0183_parse_rmc fills
		 * lat/lon. A merged publish fires when both GGA and RMC share a
		 * UTC. If the chip emits GGA quality=1 while RMC is still 'V'
		 * (or reports null-island coords), parse_rmc's early-exit on 'V'
		 * leaves lat/lon at their previous value (zero at first boot, or
		 * stale) while altitude advances — the caller sees valid
		 * fix_status + altitude-only motion + (0,0) coords. Observed on
		 * AT6558R (RAK WisMesh Tag) during early acquisition; Air530Z
		 * (ThinkNode M1) doesn't desync GGA/RMC this way. (0,0) is never
		 * a real fix — skip so we don't poison current_pos, persist
		 * zeros to flash, or promote consecutive_good_fixes. */
		if (data->nav_data.latitude == 0 && data->nav_data.longitude == 0) {
			LOG_DBG("GPS: Ignoring (0,0) fix — GGA/RMC desync "
				"(fix=%d sats=%d alt_mm=%d)",
				data->info.fix_status,
				data->info.satellites_cnt,
				data->nav_data.altitude);
			if (gps_current_state == GPS_STATE_ACQUIRING &&
			    consecutive_good_fixes > 0) {
				consecutive_good_fixes = 0;
			}
			k_mutex_unlock(&gps_mutex);
			return;
		}

		current_pos.latitude_ndeg = data->nav_data.latitude;
		current_pos.longitude_ndeg = data->nav_data.longitude;
		current_pos.altitude_mm = data->nav_data.altitude;
		current_pos.satellites = data->info.satellites_cnt;
		current_pos.valid = true;
		current_pos.timestamp_ms = k_uptime_get();
		current_utc = data->utc;

		/* Fix validation during acquisition */
		if (gps_current_state == GPS_STATE_ACQUIRING) {
			if (data->info.satellites_cnt >= GPS_MIN_SATELLITES) {
				consecutive_good_fixes++;
				LOG_INF("GPS: Good fix %d/%d (sats=%d) lat=%lld lon=%lld",
					consecutive_good_fixes, GPS_GOOD_FIX_COUNT,
					data->info.satellites_cnt,
					current_pos.latitude_ndeg / 1000000,
					current_pos.longitude_ndeg / 1000000);

				if (consecutive_good_fixes >= GPS_GOOD_FIX_COUNT) {
					bool duty = gps_duty_cycling();
					bool first_ever = !first_fix_acquired;

					LOG_INF("GPS: Got %d good fixes, updating location/time",
						GPS_GOOD_FIX_COUNT);

					/* Mark first fix acquired (enables timeout for future cycles) */
					first_fix_acquired = true;

					/* Mark time as synced from GPS - blocks phone time sync */
					gps_time_synced = true;
					last_fix_uptime_ms = k_uptime_get();

					if (duty) {
						/* Cancel timeout */
						k_work_cancel_delayable(&gps_timeout_work);

						/* Notify fix callback with validated position */
						if (gps_fix_cb) {
							double lat = (double)data->nav_data.latitude / 1000000000.0;
							double lon = (double)data->nav_data.longitude / 1000000000.0;
							k_mutex_unlock(&gps_mutex);
							gps_fix_cb(lat, lon, gps_get_utc_time());
						} else {
							k_mutex_unlock(&gps_mutex);
						}

						/* Defer standby to main thread — we're on the system
						 * workqueue here (GNSS callback), can't call PM suspend
						 * (modem_chat_run_script deadlocks on same workqueue). */
						atomic_or(&pending_gps_actions, GPS_ACTION_FIX_DONE);
						if (gps_event_cb) {
							gps_event_cb();
						}
						return;
					}

					/* Always-on: keep streaming (no standby). Throttle the fix
					 * callback (RTC sync + node-position update in main) to
					 * once per gps_acquire_timeout_ms — at 1Hz fixes it would
					 * otherwise fire constantly. Always fire on the first-ever
					 * fix so the clock syncs right away. current_pos (the
					 * telemetry source) is updated on every fix above. */
					consecutive_good_fixes = 0;
					bool promote = first_ever ||
						(k_uptime_get() - last_promote_ms >=
						 (int64_t)gps_acquire_timeout_ms);
					if (promote && gps_fix_cb) {
						last_promote_ms = k_uptime_get();
						double lat = (double)data->nav_data.latitude / 1000000000.0;
						double lon = (double)data->nav_data.longitude / 1000000000.0;
						k_mutex_unlock(&gps_mutex);
						gps_fix_cb(lat, lon, gps_get_utc_time());
						return;
					}
					k_mutex_unlock(&gps_mutex);
					return;
				}
			} else {
				/* Reset counter on bad fix (< 4 satellites) */
				if (consecutive_good_fixes > 0) {
					LOG_DBG("GPS: Poor fix (sats=%d), resetting counter",
						data->info.satellites_cnt);
				}
				consecutive_good_fixes = 0;
			}
		}
	} else {
		/* Don't clear current_pos — preserve last good fix for telemetry.
		 * Only reset the consecutive fix counter during acquisition. */
		if (gps_current_state == GPS_STATE_ACQUIRING && consecutive_good_fixes > 0) {
			LOG_DBG("GPS: No fix, resetting counter");
			consecutive_good_fixes = 0;
		}

		/* Periodic status at INF level so user knows NMEA is flowing.
		 * Without this, GPS is completely silent until first fix (all
		 * NMEA parsing is at DBG level in the driver). */
		if (gps_current_state == GPS_STATE_ACQUIRING) {
			static int64_t last_status_ms;
			int64_t now = k_uptime_get();
			if (now - last_status_ms >= 10000) {
				LOG_INF("GPS: Searching... sats=%d fix=%d",
					data->info.satellites_cnt,
					data->info.fix_status);
				last_status_ms = now;
			}
		}
	}

	k_mutex_unlock(&gps_mutex);
}

/* Register GNSS callback for all GNSS devices */
GNSS_DATA_CALLBACK_DEFINE(NULL, gnss_data_cb);

#if IS_ENABLED(CONFIG_GNSS_SATELLITES)
/* ========== Per-constellation satellite tally (diagnostic) ==========
 * The Zephyr GSV parser fills gnss_satellite.system from the NMEA talker ID
 * ($GPGSV/$GLGSV/$GAGSV/$GBGSV), so this is direct evidence of which
 * constellations the module is actually tracking — the only way to confirm
 * that the boot-time PMTK353 / UBX-CFG-GNSS configuration was accepted.
 * A module still in its GPS-only default yields sats_gps only.
 *
 * Counts only tracked satellites (is_tracked), not merely visible ones. */
static uint8_t sat_count[5];    /* gps, glonass, galileo, beidou, other */
static int64_t sat_seen_ms[5];  /* uptime when each bucket was last reported */

/* A constellation absent for this long is reported as zero. Long enough to
 * ride out a missed GSV cycle (they repeat at the fix rate), short enough
 * that a constellation which genuinely drops out stops being claimed. */
#define SAT_TALLY_STALE_MS 30000

static void gnss_satellites_cb(const struct device *dev,
			       const struct gnss_satellite *satellites,
			       uint16_t size)
{
	ARG_UNUSED(dev);
	uint8_t tally[5] = { 0 };
	bool seen[5] = { false };
	uint16_t callback_counts[GNSS_VISIBLE_SYSTEM_COUNT] = { 0 };
	uint32_t updated_systems = 0;

	if (!gps_enabled || gps_current_state == GPS_STATE_STANDBY) {
		return;
	}

	for (uint16_t i = 0; i < size; i++) {
		int idx;

		switch (satellites[i].system) {
		case GNSS_SYSTEM_GPS:     idx = 0; break;
		case GNSS_SYSTEM_GLONASS: idx = 1; break;
		case GNSS_SYSTEM_GALILEO: idx = 2; break;
		case GNSS_SYSTEM_BEIDOU:  idx = 3; break;
		default:                  idx = 4; break;
		}

		/* Mark the constellation as reported even when nothing in it is
		 * tracked — that is a real "zero", distinct from "not heard". */
		seen[idx] = true;
		if (satellites[i].is_tracked) {
			tally[idx]++;
		}

		uint32_t system = (uint32_t)satellites[i].system;
		if (system != 0U && (system & (system - 1U)) == 0U) {
			uint32_t index = (uint32_t)__builtin_ctz(system);
			if (index < GNSS_VISIBLE_SYSTEM_COUNT) {
				callback_counts[index]++;
				updated_systems |= BIT(index);
			}
		}
	}

	/* One GSV burst carries ONE constellation: the parser publishes each
	 * talker's group separately (satellites_length == number_of_svs for
	 * that group). So update only the buckets this burst reported —
	 * replacing all five wholesale wipes the constellations that arrived
	 * in the previous burst, which reads as G0 next to a healthy fix. */
	k_mutex_lock(&gps_mutex, K_FOREVER);
	for (int i = 0; i < 5; i++) {
		if (seen[i]) {
			sat_count[i] = tally[i];
			sat_seen_ms[i] = k_uptime_get();
		}
	}
	for (uint32_t i = 0; i < GNSS_VISIBLE_SYSTEM_COUNT; i++) {
		if ((updated_systems & BIT(i)) != 0U) {
			gnss_visible_by_system[i] = callback_counts[i];
		}
	}
	last_gnss_visible_satellites = 0;
	for (uint32_t i = 0; i < GNSS_VISIBLE_SYSTEM_COUNT; i++) {
		last_gnss_visible_satellites += gnss_visible_by_system[i];
	}
	k_mutex_unlock(&gps_mutex);
}

GNSS_SATELLITES_CALLBACK_DEFINE(NULL, gnss_satellites_cb);
#endif /* CONFIG_GNSS_SATELLITES */

/* Find and initialize GNSS device */
static const struct device *gnss_dev = NULL;

/* Multi-constellation configuration — runs ONCE at boot.
 * modem_chat_run_script() blocks on a semaphore signaled from the system
 * work queue. Calling it after a GPIO power cycle can deadlock because:
 * 1. The L76K needs ~300ms to boot after power restore
 * 2. Meanwhile the modem_chat may be processing stale UART data
 * 3. The script completion callback competes with NMEA processing
 *
 * Safe to call at boot because the driver init already ran and the chip
 * is powered and outputting NMEA. After power cycles, the L76K retains
 * constellation + fix rate settings in internal flash (PCAS commands
 * persist). So we only need to configure once. */
static bool gnss_configured = false;

/* ========== Configuration Diagnostics ==========
 * Module configuration is sent blind — nothing in the protocol path tells us
 * the module accepted it. This records what was attempted so the operator can
 * read it back over the CLI on a release build. `gps_set_diag(true)` also
 * clears gnss_configured, so the next GPS enable re-runs configuration and
 * refreshes the record. RAM only, never persisted. */
enum gps_cfg_path {
	GPS_CFG_NEVER = 0,  /* configuration has not run yet */
	GPS_CFG_API,        /* driver implements the GNSS API (air530z, lc76g, ...) */
	GPS_CFG_UART,       /* passive NMEA listener — raw PMTK + UBX sent */
	GPS_CFG_BLIND,      /* no GNSS API and no writable UART — module defaults */
};

static struct {
	uint8_t path;        /* enum gps_cfg_path */
	int8_t api_ret;      /* gnss_set_enabled_systems() result */
	int8_t rate_ret;     /* gnss_set_fix_rate() result */
	uint8_t cmds;        /* config commands written to the UART */
	uint16_t bytes;      /* bytes written to the UART */
	int64_t at_ms;       /* uptime when configuration last ran */
} gps_cfg_diag;


/* Module identification string, captured by the GNSS driver from the reply to
 * its version query (CASIC parts answer in-band as a $GPTXT sentence).
 * Weak so that boards whose driver has no version query still link — an
 * absent symbol and an empty string mean the same thing to the report.
 *
 * Its real value is not the version text: on a transport where every command
 * is written blind, a captured reply is the only positive proof that the
 * MCU's TX line reaches the module at all. */
extern "C" int zephcore_gnss_version_get(char *buf, size_t len) __attribute__((weak));

/* Count of NMEA sentences the GNSS driver has parsed. This is the one signal
 * in the whole diagnostic that cannot be misread: no satellites, a module
 * silenced by a disabled output protocol, and a module stranded at the wrong
 * baud all look identical otherwise. Non-zero means the module is alive and
 * talking at our baud, so anything still wrong is signal; zero means nothing
 * is arriving and no antenna work will change that. */
extern "C" uint32_t zephcore_gnss_rx_count(void) __attribute__((weak));

/* True only while gps_configure_via_uart() is running (see gps_uart_send). */
static bool gps_cfg_counting = false;

/* ========== Vendor-Specific Configuration Commands ==========
 *
 * The RAK WisBlock GPS slot accepts multiple modules (L76K, ZOE-M8Q, etc.)
 * and we use gnss-nmea-generic which is a passive NMEA listener — it has no
 * GNSS API for configuration.
 *
 * Strategy: send BOTH Quectel PMTK and u-blox UBX configuration commands.
 * Each module ignores the protocol it doesn't understand.
 *
 * This runs once at boot. Both modules persist config to internal flash,
 * so these are effectively no-ops on subsequent boots. */

#if HAS_GPS_UART

/* The UART the GNSS module is connected to. Works for any GNSS-on-UART node
 * regardless of compatible string. */
static const struct device *gps_uart_dev = DEVICE_DT_GET(DT_BUS(DT_NODELABEL(gnss)));

/* Send raw bytes to the GPS UART using blocking poll_out.
 * Safe to call even though modem_chat/modem_ubx owns the UART pipe:
 * uart_poll_out writes one byte at a time through the TX register,
 * and GNSS modules are receive-only (no TX contention).
 *
 * Gated on HAS_GPS_UART alone — writing to the module is safe on every
 * UART-attached GNSS. The narrower power-control gate below applies to the
 * software *sleep* commands, which are only a fallback for boards that
 * cannot cut GPS power. */
static void gps_uart_send(const uint8_t *data, size_t len)
{
	if (!device_is_ready(gps_uart_dev)) {
		return;
	}
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(gps_uart_dev, data[i]);
	}
	/* Count only configuration traffic — the same helper carries the
	 * sleep/wake commands, which would otherwise inflate the tally. */
	if (gps_cfg_counting) {
		gps_cfg_diag.cmds++;
		gps_cfg_diag.bytes += (uint16_t)len;
	}
}

/* --- MediaTek-family (PMTK) configuration ---
 *
 * PMTK is MediaTek's protocol. It applies to genuine MTK parts (L76B and
 * relatives). It does NOT apply to the Quectel L76K/L76KB or Air530Z, which
 * are CASIC silicon and speak PCAS — their protocol specification contains no
 * PMTK command at all, so these sentences are inert there. Those modules are
 * driven by the air530z driver via the GNSS API instead and never reach this
 * path. Kept because the RAK WisBlock GPS slot can hold an MTK part. */

/* PMTK353: Enable GPS + GLONASS + Galileo + BeiDou (no QZSS).
 * Default is GPS-only. Multi-constellation dramatically improves TTFF
 * and fix reliability, especially indoors or with limited sky view. */
static const char pmtk_constellations[] = "$PMTK353,1,1,1,1,0*2B\r\n";

/* PMTK869: Enable EASY (Embedded Assist System).
 * Caches predicted satellite ephemeris in the GNSS module's internal flash.
 * Reduces TTFF from 15-45s (cold) to 1-3s (warm) for up to 3 days after
 * last fix. Setting persists in flash — resending is a harmless no-op. */
static const char pmtk_easy[] = "$PMTK869,1,1*35\r\n";

/* PMTK286: Enable AIC (Active Interference Cancellation).
 * Filters out narrowband jammers (e.g. harmonics from nearby electronics,
 * LoRa radio leakage). Improves sensitivity by ~2dB in noisy environments.
 * Especially useful when GPS antenna is near the SX1262 + SKY66122 PA. */
static const char pmtk_aic[] = "$PMTK286,1*23\r\n";

/* --- CASIC (PCAS) configuration ---
 *
 * The Quectel L76K/L76KB (RAK12501) and Air530Z are CASIC silicon: they speak
 * neither PMTK nor UBX, so without these sentences such a module in a
 * WisBlock slot receives no configuration at all and sits on its factory
 * defaults. Boards that always carry one use the air530z driver and the GNSS
 * API instead; these are for the generic-NMEA boards whose GPS slot can hold
 * any module.
 *
 * Inert on the other families, same as PMTK and UBX are here. */

/* PCAS03: NMEA sentence selection. Field order is
 * GGA,GLL,GSA,GSV,RMC,VTG,ZDA,ANT,... — keep GGA + RMC (position, time) and
 * add GSV only when the satellite tally needs it, to keep the 9600-baud link
 * from spending its budget on sentences nobody parses. */
#ifdef CONFIG_ZEPHCORE_GPS_SAT_DIAG
static const char pcas_sentences[] = "$PCAS03,1,0,0,1,1,0,0,0,0,0,0,0,0*1F\r\n";
#else
static const char pcas_sentences[] = "$PCAS03,1,0,0,0,1,0,0,0,0,0,0,0,0*1E\r\n";
#endif

/* PCAS04,7 = GPS + BeiDou + GLONASS, everything the part supports.
 * (No Galileo on these modules — that is silicon, not configuration.) */
static const char pcas_constellations[] = "$PCAS04,7*1E\r\n";

/* PCAS11: navigation dynamic model. Stored IN THE MODULE and survives
 * reflashing the host, so a slot module that previously lived in another
 * device can arrive stuck in an automotive or airborne model that quietly
 * degrades fixes on a fixed site. See CONFIG_ZEPHCORE_GPS_NAV_MODE. */
#if CONFIG_ZEPHCORE_GPS_NAV_MODE == 0
static const char pcas_nav_mode[] = "$PCAS11,0*1D\r\n";
#elif CONFIG_ZEPHCORE_GPS_NAV_MODE == 1
static const char pcas_nav_mode[] = "$PCAS11,1*1C\r\n";
#elif CONFIG_ZEPHCORE_GPS_NAV_MODE == 2
static const char pcas_nav_mode[] = "$PCAS11,2*1F\r\n";
#elif CONFIG_ZEPHCORE_GPS_NAV_MODE == 3
static const char pcas_nav_mode[] = "$PCAS11,3*1E\r\n";
#elif CONFIG_ZEPHCORE_GPS_NAV_MODE == 4
static const char pcas_nav_mode[] = "$PCAS11,4*19\r\n";
#elif CONFIG_ZEPHCORE_GPS_NAV_MODE == 5
static const char pcas_nav_mode[] = "$PCAS11,5*18\r\n";
#elif CONFIG_ZEPHCORE_GPS_NAV_MODE == 6
static const char pcas_nav_mode[] = "$PCAS11,6*1B\r\n";
#elif CONFIG_ZEPHCORE_GPS_NAV_MODE == 7
static const char pcas_nav_mode[] = "$PCAS11,7*1A\r\n";
#endif

/* PCAS06,0: ask the module to identify itself. A CASIC part answers in-band
 * with a $GPTXT sentence — the only readable reply available on this
 * otherwise write-only path, and therefore the only positive proof that the
 * MCU's TX line reaches the module at all. Captured by the GNSS driver and
 * surfaced as `mod=` in "get gps diag". */
static const char pcas_version_query[] = "$PCAS06,0*1B\r\n";

/* u-blox equivalent of the query above. u-blox ignore $PCAS06 entirely, so
 * without this a u-blox module always reports "no reply" and the TX-path
 * proof — the whole point of asking — is unavailable on those boards. The
 * $PUBX,04 reply carries time and clock status, not a version; what matters
 * is that a reply arrives at all, which only happens if the module received
 * the request. */
static const char pubx_version_query[] = "$PUBX,04*37\r\n";

/* --- u-blox NMEA output trim ($PUBX,40) ---
 *
 * THE LINK BUDGET IS THE CONSTRAINT, and it is easy to blow past it. At 9600
 * baud only ~960 bytes/s fit. With multi-GNSS enabled a u-blox emits, per
 * second: GGA + RMC + GLL + VTG, one GSA per constellation, and a GSV burst
 * that grows with satellite count — roughly 1030 bytes/s at ~26 SVs. The
 * stream then cannot fit in the second it is generated in, sentences are
 * truncated or dropped, and the symptom is not "slow GPS" but a receiver
 * that appears to have stopped: no parseable GGA, so no fix, so no position.
 *
 * Enabling constellations without trimming output is therefore actively
 * harmful on a 9600-baud link. We already do exactly this trim for CASIC
 * parts via $PCAS03; u-blox had no equivalent, which is the asymmetry this
 * fixes. GLL, GSA and VTG are dropped outright — nothing in the driver parses
 * them — and GSV is kept only when the satellite tally needs it.
 *
 * Dropping GLL/GSA/VTG takes ~1030 -> ~650 bytes/s; dropping GSV as well
 * takes it to ~160. */
static const char pubx_off_gll[] = "$PUBX,40,GLL,0,0,0,0,0,0*5C\r\n";
static const char pubx_off_gsa[] = "$PUBX,40,GSA,0,0,0,0,0,0*4E\r\n";
static const char pubx_off_vtg[] = "$PUBX,40,VTG,0,0,0,0,0,0*5E\r\n";
#ifndef CONFIG_ZEPHCORE_GPS_SAT_DIAG
static const char pubx_off_gsv[] = "$PUBX,40,GSV,0,0,0,0,0,0*59\r\n";
#endif

/* --- u-blox ZOE-M8Q (UBX binary) configuration --- */

/* UBX-CFG-PRT: force UART1 to 9600 8N1 with BOTH UBX and NMEA enabled in and
 * out. Sent first, before anything that depends on the module talking to us.
 *
 * This exists because the module's port configuration is persistent and not
 * necessarily ours. RAK's own RAK12500 example — and any host using the
 * SparkFun u-blox library — calls setUART1Output(COM_TYPE_UBX) followed by
 * saveConfiguration(), which stores "UBX only, NMEA off" in the module's
 * flash. A module that has ever been driven that way stays silent on an
 * NMEA-only host forever after, through power cycles and reflashes, and
 * presents as a completely dead receiver: no GGA, no fix, no reply to any
 * query. Re-asserting the port configuration costs one frame and removes a
 * failure mode that is otherwise almost impossible to diagnose from the host.
 *
 * Limitation: if the module was also saved at a different baud rate, it will
 * not parse this frame either. Recovering from that needs a baud scan, which
 * the devicetree's fixed current-speed does not currently allow. */
static const uint8_t ubx_cfg_prt_uart1[] = {
	0xB5, 0x62, 0x06, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0xC0, 0x08,
	0x00, 0x00, 0x80, 0x25, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x8E, 0x95
};

/* UBX-CFG-GNSS: Enable GPS + Galileo + GLONASS (+ QZSS) on u-blox M8.
 *
 * Config block layout is gnssId, resTrkCh, maxTrkCh, reserved0, flags[4] —
 * EIGHT bytes, and the payload length must be exactly 4 + 8*numConfigBlocks
 * or the receiver rejects the whole message. The previous version of this
 * frame omitted reserved0, giving 7-byte blocks and a 39-byte payload where
 * numConfigBlocks=5 demanded 44, so no u-blox module ever accepted it.
 *
 * Only THREE major GNSS (GPS/Galileo/GLONASS/BeiDou) can run concurrently on
 * M8, so BeiDou is explicitly disabled rather than left alone: if the module
 * came up with BeiDou on, enabling three others would make four and the
 * message would be refused.
 *
 * QZSS is enabled even though it is Japan-regional and costs ~3 channels —
 * u-blox require GPS and QZSS to be both enabled or both disabled (they
 * share L1 C/A), and a mismatch is grounds for rejection.
 *
 * SBAS stays off: it needs 30-60 s to download corrections, useless for our
 * quick-fix-then-sleep pattern (companions 30 s, repeaters 5 min).
 *
 * numTrkChHw = 0 and numTrkChUse = 0xFF (read-only / "use max available").
 *
 * resTrkCh MUST be 0 on the disabled blocks. Reserving tracking channels for
 * a system whose enable bit is clear is self-contradictory, and the receiver
 * validates CFG-GNSS atomically — one bad block rejects all six. An earlier
 * revision left SBAS at 1 and BeiDou at 8 and the whole frame was refused
 * (observed on hardware: RAK12500/ZOE-M8Q stayed GPS-only, sys=G8/R0/E0/B0).
 *
 * NOTE — this is an M8 frame. u-blox 7 parts (MAX-7Q on the RAK1910) have no
 * Galileo or BeiDou and use a different sigCfgMask, so they will refuse it
 * and stay GPS-only. Sending an M7 frame as well is NOT safe blind: its
 * sigCfgMask of 0 would be a signal-disabling value on M8. */
static const uint8_t ubx_cfg_gnss[] = {
	0xB5, 0x62, 0x06, 0x3E, 0x34, 0x00, 0x00, 0x00, 0xFF, 0x06, 0x00, 0x08,
	0x10, 0x00, 0x01, 0x00, 0x01, 0x01, 0x01, 0x00, 0x03, 0x00, 0x00, 0x00,
	0x01, 0x01, 0x02, 0x04, 0x08, 0x00, 0x01, 0x00, 0x01, 0x01, 0x03, 0x00,
	0x10, 0x00, 0x00, 0x00, 0x01, 0x01, 0x05, 0x00, 0x03, 0x00, 0x01, 0x00,
	0x01, 0x01, 0x06, 0x08, 0x0E, 0x00, 0x01, 0x00, 0x01, 0x01, 0xEE, 0x64
};

/* UBX-CFG-NMEA: switch the NMEA output to version 4.10.
 *
 * Without this, Galileo and BeiDou satellites cannot be reported at all: the
 * $GAGSV and $GBGSV talker IDs only exist from NMEA 4.10, and M8 firmware
 * defaults lower. So a fully successful CFG-GNSS would still show zero
 * Galileo in "get gps diag" — a reporting limit masquerading as a config
 * failure. Mirrors Meshtastic's "enable NMEA 4.10" step (src/gps/ubx.h).
 *
 * gsvTalkerId = 0 (use the GNSS-specific talker per constellation) is what
 * makes the per-constellation tally work — do not set it to 1, which forces
 * every GSV onto the main talker and would collapse the tally into GPS. */
static const uint8_t ubx_cfg_nmea_410[] = {
	0xB5, 0x62, 0x06, 0x17, 0x14, 0x00, 0x00, 0x41, 0x00, 0x02, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x75, 0x57
};

/* UBX-CFG-NAV5: Set 5° minimum satellite elevation.
 * Ignore satellites below 5° elevation — they have more atmospheric
 * noise and multipath, degrading fix quality. The default 0° lets in
 * everything including horizon-level junk.
 * Dynamic model left at factory default (Portable) — works for fixed
 * repeaters, walking companions, and vehicles alike.
 * apply mask 0x0002 = minEl(bit1) only
 *
 * The trailing checksum was CK_B=0x37 (should be 0xE7) — a one-byte typo
 * that made every receiver drop this frame silently, with no NAK. */
static const uint8_t ubx_cfg_nav5_minelev[] = {
	0xB5, 0x62, 0x06, 0x24, 0x24, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0xE7
};

/* UBX-CFG-NAVX5: Enable AssistNow Autonomous (AOP).
 * u-blox equivalent of Quectel EASY — the receiver autonomously predicts
 * satellite orbits from previously downloaded ephemeris data. Predictions
 * stay valid for 3-6 days, reducing TTFF from 26-30s (cold) to 2-5s.
 * No server connection needed — runs entirely on-chip.
 * mask1 bit 14 = aop, aopCfg bit 0 = enable. */
static const uint8_t ubx_cfg_navx5_aop[] = {
	0xB5, 0x62, 0x06, 0x23, 0x28, 0x00, 0x04, 0x00, 0x00, 0x40, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x96, 0x66
};

/* UBX-CFG-CFG: Save all configuration to BBR + Flash + EEPROM.
 * Persists constellation, nav model, SBAS settings across power cycles
 * and backup mode. Without this, ZOE-M8Q reverts to factory defaults
 * after a full power loss (though BBR survives backup mode). */
static const uint8_t ubx_cfg_save[] = {
	0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17, 0x31, 0xBF
};

/* Settle time after a command that makes the receiver restart its navigation
 * engine — constellation changes on every family do this. A command issued
 * into a restarting engine is simply lost, and because everything here is
 * written blind the loss is silent. 20-50 ms was optimistic. */
#define GPS_CFG_SETTLE_MS      120
#define GPS_CFG_RESTART_MS     500

/* Send an NMEA command string (including \r\n). */
static void gps_send_nmea(const char *cmd, uint32_t settle_ms = GPS_CFG_SETTLE_MS)
{
	gps_uart_send((const uint8_t *)cmd, strlen(cmd));
	k_msleep(settle_ms);
}

/* Send a UBX binary frame. */
static void gps_send_ubx(const uint8_t *frame, size_t len,
			 uint32_t settle_ms = GPS_CFG_SETTLE_MS)
{
	gps_uart_send(frame, len);
	k_msleep(settle_ms);
}

/* u-blox dynamic model for CFG-NAV5, translated from the CASIC-numbered
 * CONFIG_ZEPHCORE_GPS_NAV_MODE so one setting drives both families.
 * u-blox: 0 portable, 2 stationary, 3 pedestrian, 4 automotive, 5 sea,
 * 6-8 airborne. */
#if   CONFIG_ZEPHCORE_GPS_NAV_MODE == 1
#define UBX_DYNMODEL 2
#elif CONFIG_ZEPHCORE_GPS_NAV_MODE == 2
#define UBX_DYNMODEL 3
#elif CONFIG_ZEPHCORE_GPS_NAV_MODE == 3
#define UBX_DYNMODEL 4
#elif CONFIG_ZEPHCORE_GPS_NAV_MODE == 4
#define UBX_DYNMODEL 5
#elif CONFIG_ZEPHCORE_GPS_NAV_MODE >= 5
#define UBX_DYNMODEL 6
#elif CONFIG_ZEPHCORE_GPS_NAV_MODE == 0
#define UBX_DYNMODEL 0
#endif

/* Send CFG-NAV5 with the dynamic model patched in and the checksum redone.
 * A stationary model on a fixed node suppresses position wander and lets the
 * receiver apply much tighter velocity assumptions; leaving a roof-mounted
 * repeater on the factory Portable model throws that away. Patched at runtime
 * rather than as eight hard-coded frames — one place to get the checksum
 * right instead of eight. */
#ifdef UBX_DYNMODEL
static void gps_send_ubx_nav5(void)
{
	uint8_t f[sizeof(ubx_cfg_nav5_minelev)];

	memcpy(f, ubx_cfg_nav5_minelev, sizeof(f));
	f[6] = 0x03;            /* mask: dyn (bit0) + minEl (bit1) */
	f[8] = UBX_DYNMODEL;    /* dynModel */

	uint8_t ck_a = 0, ck_b = 0;
	for (size_t i = 2; i < sizeof(f) - 2; i++) {
		ck_a = (uint8_t)(ck_a + f[i]);
		ck_b = (uint8_t)(ck_b + ck_a);
	}
	f[sizeof(f) - 2] = ck_a;
	f[sizeof(f) - 1] = ck_b;

	gps_send_ubx(f, sizeof(f));
}
#endif

/* Configure the GPS module with optimal settings for a mesh repeater.
 * Sends both PMTK (Quectel) and UBX (u-blox) commands — the module that
 * isn't present ignores bytes it doesn't understand. */
static void gps_configure_via_uart(void)
{
	LOG_INF("GPS: Configuring via UART (PMTK + UBX dual-protocol)");

	gps_cfg_diag.cmds = 0;
	gps_cfg_diag.bytes = 0;
	gps_cfg_counting = true;

	/* --- MediaTek-family (PMTK) --- */
	gps_send_nmea(pmtk_constellations, GPS_CFG_RESTART_MS);
	gps_send_nmea(pmtk_easy);
	gps_send_nmea(pmtk_aic);
	LOG_INF("GPS: PMTK config sent (constellations, EASY, AIC)");

	/* --- CASIC (PCAS) ---
	 * Version query last, so its $GPTXT reply is not stepped on by a
	 * constellation restart. */
	gps_send_nmea(pcas_sentences);
	gps_send_nmea(pcas_constellations, GPS_CFG_RESTART_MS);
#if CONFIG_ZEPHCORE_GPS_NAV_MODE >= 0
	gps_send_nmea(pcas_nav_mode);
#endif
	gps_send_nmea(pcas_version_query);
	LOG_INF("GPS: PCAS config sent (sentences, constellations, nav mode, version query)");

	/* --- u-blox ZOE-M8Q (UBX) ---
	 * NMEA 4.10 goes first: it governs whether the constellations enabled
	 * by the next frame can be *reported* at all. CFG-GNSS restarts the
	 * navigation engine, so it gets the long settle before the frames that
	 * follow it — at 50 ms they were being issued into a restarting
	 * receiver. CFG-CFG stays last so it only persists whatever was
	 * actually accepted; note that makes a rejected configuration sticky
	 * too, which is why a frame bug here survives power cycles. */
	/* Make sure NMEA output is even switched on before anything else — a
	 * module saved as UBX-only by a previous host is otherwise mute. */
	gps_send_ubx(ubx_cfg_prt_uart1, sizeof(ubx_cfg_prt_uart1), GPS_CFG_RESTART_MS);

	/* Trim the NMEA stream BEFORE enabling more constellations — the extra
	 * GSV traffic must have somewhere to fit. */
	gps_send_nmea(pubx_off_gll);
	gps_send_nmea(pubx_off_gsa);
	gps_send_nmea(pubx_off_vtg);
#ifndef CONFIG_ZEPHCORE_GPS_SAT_DIAG
	gps_send_nmea(pubx_off_gsv);
#endif
	gps_send_ubx(ubx_cfg_nmea_410, sizeof(ubx_cfg_nmea_410));
	gps_send_ubx(ubx_cfg_gnss, sizeof(ubx_cfg_gnss), GPS_CFG_RESTART_MS);
#ifdef UBX_DYNMODEL
	gps_send_ubx_nav5();
#else
	gps_send_ubx(ubx_cfg_nav5_minelev, sizeof(ubx_cfg_nav5_minelev));
#endif
	gps_send_ubx(ubx_cfg_navx5_aop, sizeof(ubx_cfg_navx5_aop));
	gps_send_ubx(ubx_cfg_save, sizeof(ubx_cfg_save));
	/* Ask a u-blox to say something back — the TX-path proof for this
	 * family, sent last so the reply is not stepped on by a restart. */
	gps_send_nmea(pubx_version_query);
	LOG_INF("GPS: UBX config sent (NMEA 4.10, multi-GNSS, 5° min elev, AOP, saved)");

	gps_cfg_counting = false;
}
#endif /* HAS_GPS_UART */

/* Re-run module configuration on a GPS enable, but only when diagnostics are
 * armed — this is a deliberate, operator-triggered action, not a normal path.
 *
 * Only the raw-UART path is re-runnable. gnss_configure()'s API path goes
 * through modem_chat_run_script(), which is safe at boot only: after a GPIO
 * power restore the chip needs ~300 ms and calling it here deadlocks the main
 * thread. So on API-driver boards this records nothing new and "get gps diag"
 * keeps reporting the boot-time result, which is the honest answer. */
static void gps_diag_maybe_reconfigure(void)
{
	if (!gps_diag_on || gnss_configured || gnss_dev == NULL) {
		return;
	}
#if HAS_GPS_UART
	if (gps_cfg_diag.path == GPS_CFG_UART || gps_cfg_diag.path == GPS_CFG_NEVER) {
		/* The module has just been powered; give it time to boot before
		 * clocking configuration at it (same ~300 ms the modem needs). */
		k_msleep(300);
		gps_cfg_diag.path = GPS_CFG_UART;
		gps_cfg_diag.at_ms = k_uptime_get();
		gps_configure_via_uart();
	}
#endif
	gnss_configured = true;
}

static void gnss_configure(void)
{
	if (gnss_configured || gnss_dev == NULL) {
		return;
	}

	/* Enable all available constellation systems for faster TTFF.
	 * Try GPS+GLONASS+Galileo+BeiDou first (AG3335 supports all).
	 * Fall back to GPS+GLONASS+BeiDou if Galileo not supported (L76KB). */
	gnss_systems_t systems = GNSS_SYSTEM_GPS | GNSS_SYSTEM_GLONASS |
				 GNSS_SYSTEM_GALILEO | GNSS_SYSTEM_BEIDOU;
	int ret = gnss_set_enabled_systems(gnss_dev, systems);
	if (ret == -EINVAL) {
		/* Some systems not supported — try without Galileo */
		systems = GNSS_SYSTEM_GPS | GNSS_SYSTEM_GLONASS | GNSS_SYSTEM_BEIDOU;
		ret = gnss_set_enabled_systems(gnss_dev, systems);
	}
	gps_cfg_diag.api_ret = (int8_t)ret;
	gps_cfg_diag.at_ms = k_uptime_get();

	if (ret == 0) {
		LOG_INF("GPS: Multi-constellation enabled via GNSS API");
		gps_cfg_diag.path = GPS_CFG_API;
	} else if (ret == -ENOSYS || ret == -ENOTSUP) {
#if HAS_GPS_UART
		/* gnss-nmea-generic is a passive listener — no GNSS API.
		 * Configure everything via direct UART commands instead. */
		gps_cfg_diag.path = GPS_CFG_UART;
		gps_configure_via_uart();
#else
		LOG_INF("GPS: No GNSS API and no UART access — using module defaults");
		gps_cfg_diag.path = GPS_CFG_BLIND;
#endif
	} else {
		LOG_WRN("GPS: Failed to set constellations: %d", ret);
		/* Will retry on next power-on cycle */
		return;
	}

	/* Set 1Hz fix rate (explicit, don't rely on chip defaults) */
	ret = gnss_set_fix_rate(gnss_dev, 1000);
	gps_cfg_diag.rate_ret = (int8_t)ret;
	if (ret == 0) {
		LOG_INF("GPS: Fix rate set to 1Hz");
	} else if (ret != -ENOSYS && ret != -ENOTSUP) {
		LOG_WRN("GPS: Failed to set fix rate: %d", ret);
	}

	gnss_configured = true;
}

#endif /* HAS_GNSS - GPS power GPIO section is unconditional (needed for shutdown) */

/* ========== GPS Power GPIO Control ==========
 * These are unconditional (not gated by HAS_GNSS) because
 * gps_power_off_for_shutdown() must be available for System OFF
 * even on boards without a GNSS driver.
 *
 * IMPORTANT: Do NOT touch GPIO during init! The GNSS driver needs the GPS
 * to be powered and outputting NMEA for the modem pipe to work.
 * We only configure GPIO lazily on first power-off request.
 *
 * Board-specific pins (defined in board overlays as gps-enable alias):
 * - T1000-E: P1.11 (GPS_EN), P0.8 (GPS_VRTC_EN), P1.15 (GPS_RESET), P1.12 (GPS_SLEEP_INT)
 * - Wio Tracker L1: P1.09 (GPS power, shared with luatos,air530z on-off-gpios)
 */
#if HAS_GPS_POWER_CONTROL
static const struct gpio_dt_spec gps_enable_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(gps_enable), gpios);
#endif

/* GPS powered by a PMU regulator rail instead of a discrete enable GPIO (e.g.
 * LilyGo T-Beam: GPS is on the AXP2101 ALDO3 rail). Selected via the chosen
 * `zephcore,gps-power` node pointing at the regulator. Acts as a master power
 * switch driven by enable/disable; the duty-cycle standby/wake uses software
 * sleep/wake (UART) and leaves the rail up, so the regulator is only toggled on
 * the (unguarded) enable/disable/boot paths — never per duty cycle. */
#if HAS_GPS_POWER_REGULATOR
static const struct device *const gps_power_reg =
	DEVICE_DT_GET(DT_CHOSEN(zephcore_gps_power));
/* Tracks our intended rail state so enable/disable stay balanced (idempotent).
 * Starts true: the rail is `regulator-boot-on`, so it is already up at boot. */
static bool gps_reg_enabled = true;
#endif

/* AXP2101 backup (button-battery) charger — feeds the GPS receiver's V_BCKP
 * domain so ephemeris/RTC survive main-rail (ALDO3) power cuts, giving a
 * warm/hot re-fix instead of a cold start each duty cycle. The Zephyr regulator
 * driver doesn't expose VBACKUP, so enable it with raw I2C at boot (mirrors
 * Arduino enablePowerOutput(XPOWERS_VBACKUP) + setPowerChannelVoltage 3.3V).
 * Selected via chosen `zephcore,gps-backup-pmu` pointing at the AXP2101 node. */
#if DT_NODE_EXISTS(DT_CHOSEN(zephcore_gps_backup_pmu))
#define AXP2101_REG_CHG_GAUGE_WDT_CTRL  0x18U  /* bit 2 = button-battery charge enable */
#define AXP2101_BTN_CHARGE_ENABLE       BIT(2)
#define AXP2101_REG_BTN_BAT_CHG_VOL_SET 0x6AU  /* low 3 bits: (mV - 2600) / 100 */
#define AXP2101_BTN_VOL_3V3             0x07U  /* (3300 - 2600) / 100 */
static int gps_backup_charger_init(void)
{
	static const struct i2c_dt_spec axp = I2C_DT_SPEC_GET(DT_CHOSEN(zephcore_gps_backup_pmu));

	if (!device_is_ready(axp.bus)) {
		LOG_WRN("GPS backup: AXP2101 I2C bus not ready");
		return 0;
	}
	/* Set the backup-charge target to 3.3V (low 3 bits), then enable the
	 * charger. Read-modify-write so the fuel-gauge enable (bit 3 of 0x18) and
	 * the other 0x6A bits are preserved. */
	i2c_reg_update_byte_dt(&axp, AXP2101_REG_BTN_BAT_CHG_VOL_SET, 0x07U, AXP2101_BTN_VOL_3V3);
	i2c_reg_update_byte_dt(&axp, AXP2101_REG_CHG_GAUGE_WDT_CTRL,
			       AXP2101_BTN_CHARGE_ENABLE, AXP2101_BTN_CHARGE_ENABLE);
	LOG_INF("GPS backup: AXP2101 VBACKUP charger enabled (3.3V)");
	return 0;
}
/* After the MFD/I2C is up (POST_KERNEL ~86); APPLICATION is safely later. */
SYS_INIT(gps_backup_charger_init, APPLICATION, 50);
#endif

/* T1000-E specific GPS control pins */
#if DT_NODE_EXISTS(DT_ALIAS(gps_vrtc_enable))
static const struct gpio_dt_spec gps_vrtc_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(gps_vrtc_enable), gpios);
#define HAS_GPS_VRTC 1
#else
#define HAS_GPS_VRTC 0
#endif

#if DT_NODE_EXISTS(DT_ALIAS(gps_reset))
static const struct gpio_dt_spec gps_reset_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(gps_reset), gpios);
#define HAS_GPS_RESET 1
#else
#define HAS_GPS_RESET 0
#endif

#if DT_NODE_EXISTS(DT_ALIAS(gps_sleep_int))
static const struct gpio_dt_spec gps_sleep_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(gps_sleep_int), gpios);
#define HAS_GPS_SLEEP 1
#else
#define HAS_GPS_SLEEP 0
#endif

/* GPS RTC interrupt pin — held LOW during normal operation */
#if DT_NODE_EXISTS(DT_ALIAS(gps_rtc_int))
static const struct gpio_dt_spec gps_rtcint_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(gps_rtc_int), gpios);
#define HAS_GPS_RTCINT 1
#else
#define HAS_GPS_RTCINT 0
#endif

/* GPS RESETB (active-LOW reset) — must be INPUT_PULLUP for normal operation.
 * Without the pull-up, this pin floats LOW and holds the AG3335 in permanent
 * reset, preventing any UART output. */
#if DT_NODE_EXISTS(DT_ALIAS(gps_resetb))
static const struct gpio_dt_spec gps_resetb_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(gps_resetb), gpios);
#define HAS_GPS_RESETB 1
#else
#define HAS_GPS_RESETB 0
#endif

/* T1000-E has extra GPS control pins that require a specific init sequence */
#define HAS_T1000_GPS_CONTROL (HAS_GPS_VRTC || HAS_GPS_RESET || HAS_GPS_SLEEP)

#if HAS_GPS_POWER_CONTROL
static bool gps_gpio_configured = false;
#endif

/* GPS power control with warm standby support.
 * @param on        true = power on, false = power off
 * @param keep_vrtc When powering off: true = keep VRTC alive (warm standby,
 *                  preserves ephemeris/almanac/RTC for fast re-acquisition),
 *                  false = full power-off (cold start on next wake).
 *                  Only relevant on T1000-E (HAS_GPS_VRTC); ignored on other boards. */
static void gps_power_control(bool on, bool keep_vrtc = false)
{
#if HAS_GPS_POWER_REGULATOR
	/* Master power rail (PMU regulator). Idempotent enable/disable so the
	 * refcount stays balanced regardless of how often this is called. */
	if (on != gps_reg_enabled && device_is_ready(gps_power_reg)) {
		int ret = on ? regulator_enable(gps_power_reg)
			     : regulator_disable(gps_power_reg);
		if (ret == 0) {
			gps_reg_enabled = on;
			LOG_INF("GPS power %s (regulator)", on ? "ON" : "OFF");
		} else {
			LOG_WRN("GPS regulator %s failed: %d", on ? "enable" : "disable", ret);
		}
	}
#endif
#if HAS_GPS_POWER_CONTROL
	/* Direct GPIO power control — works on all boards.
	 * We toggle the GPS power pin ourselves rather than using driver PM
	 * (driver PM can hang on modem_pipe_close / modem_chat_run_script).
	 * The GNSS driver's modem pipe stays open.
	 *
	 * T1000-E (HAS_GPS_VRTC): Use warm standby (keep VRTC) for app toggle
	 *   so UART/chip state is preserved. Matches Arduino sleep_gps().
	 * Simple boards (Wio etc.): Full power off/on via GPS_EN. */
	if (on) {
#if HAS_T1000_GPS_CONTROL
		/* T1000-E power-on sequence (from Arduino target.cpp start_gps())
		 * Must follow this exact order with delays:
		 * 1. GPS_EN HIGH, delay 10ms
		 * 2. GPS_VRTC_EN HIGH, delay 10ms (critical - RTC power)
		 * 3. GPS_RESET HIGH, delay 10ms, then LOW
		 * 4. GPS_SLEEP_INT HIGH
		 */
		if (gpio_is_ready_dt(&gps_enable_gpio)) {
			gpio_pin_configure_dt(&gps_enable_gpio, GPIO_OUTPUT_HIGH);
		}
		k_msleep(10);

#if HAS_GPS_VRTC
		if (gpio_is_ready_dt(&gps_vrtc_gpio)) {
			gpio_pin_configure_dt(&gps_vrtc_gpio, GPIO_OUTPUT_HIGH);
		}
		k_msleep(10);
#endif

#if HAS_GPS_RESET
		if (gpio_is_ready_dt(&gps_reset_gpio)) {
			gpio_pin_configure_dt(&gps_reset_gpio, GPIO_OUTPUT_HIGH);
			k_msleep(10);
			gpio_pin_set_dt(&gps_reset_gpio, 0);  /* Release reset */
		}
#endif

#if HAS_GPS_SLEEP
		if (gpio_is_ready_dt(&gps_sleep_gpio)) {
			gpio_pin_configure_dt(&gps_sleep_gpio, GPIO_OUTPUT_HIGH);
		}
#endif

#if HAS_GPS_RTCINT
		/* GPS_RTC_INT (P0.15) — held LOW during normal operation */
		if (gpio_is_ready_dt(&gps_rtcint_gpio)) {
			gpio_pin_configure_dt(&gps_rtcint_gpio, GPIO_OUTPUT_LOW);
		}
#endif

#if HAS_GPS_RESETB
		/* GPS_RESETB (P1.14) — active-LOW reset, must be pulled HIGH.
		 * INPUT_PULLUP de-asserts reset so the AG3335 can boot.
		 * Without this the pin floats LOW → chip stuck in reset → no UART. */
		if (gpio_is_ready_dt(&gps_resetb_gpio)) {
			gpio_pin_configure_dt(&gps_resetb_gpio, GPIO_INPUT | GPIO_PULL_UP);
		}
#endif
		gps_gpio_configured = true;
		LOG_INF("GPS power ON (T1000-E sequence)");
#else
		/* Simple boards - just GPS_EN */
		if (!gps_gpio_configured) {
			if (gpio_is_ready_dt(&gps_enable_gpio)) {
				gpio_pin_configure_dt(&gps_enable_gpio, GPIO_OUTPUT_HIGH);
				gps_gpio_configured = true;
				LOG_INF("GPS power GPIO configured, set HIGH");
			} else {
				LOG_WRN("GPS power GPIO not ready");
				return;
			}
		} else {
			gpio_pin_set_dt(&gps_enable_gpio, 1);
			LOG_INF("GPS power ON");
		}
#endif
	} else {
		/* Power off sequence */
#if HAS_GPS_RESET
		/* Hold GPS in reset during power-off — matches Arduino sleep_gps()/stop_gps().
		 * Ensures chip sees RESET asserted when GPS_EN goes HIGH on next
		 * power-on, preventing uncontrolled startup before the reset pulse.
		 * Configure-on-first-use (mirrors the GPS_EN pin below): on a
		 * boot-with-GPS-off the power-on path never ran, so the pin isn't an
		 * output yet — gpio_pin_set_dt() alone would leave it floating instead
		 * of asserting reset. GPIO_OUTPUT_ACTIVE drives the active (asserted)
		 * level directly. */
		if (gpio_is_ready_dt(&gps_reset_gpio)) {
			if (!gps_gpio_configured) {
				gpio_pin_configure_dt(&gps_reset_gpio, GPIO_OUTPUT_ACTIVE);
			} else {
				gpio_pin_set_dt(&gps_reset_gpio, 1);
			}
		}
#endif

#if HAS_GPS_VRTC
		if (!keep_vrtc) {
			/* Full power-off: VRTC off too (cold start on next wake) */
			if (gpio_is_ready_dt(&gps_vrtc_gpio)) {
				if (!gps_gpio_configured) {
					gpio_pin_configure_dt(&gps_vrtc_gpio, GPIO_OUTPUT_LOW);
				} else {
					gpio_pin_set_dt(&gps_vrtc_gpio, 0);
				}
			}
		}
		/* else: warm standby — VRTC stays HIGH, preserving
		 * ephemeris/almanac/RTC for fast re-acquisition (~1-2 µA) */
#endif
		if (gpio_is_ready_dt(&gps_enable_gpio)) {
			if (!gps_gpio_configured) {
				gpio_pin_configure_dt(&gps_enable_gpio, GPIO_OUTPUT_LOW);
				gps_gpio_configured = true;
			} else {
				gpio_pin_set_dt(&gps_enable_gpio, 0);
			}
		}

#if HAS_GPS_RESETB
		/* Drive RESETB LOW when GPS is off (Arduino sleep_gps/stop_gps) */
		if (gpio_is_ready_dt(&gps_resetb_gpio)) {
			gpio_pin_configure_dt(&gps_resetb_gpio, GPIO_OUTPUT_LOW);
		}
#endif

#if HAS_GPS_RTCINT
		/* GPS_RTC_INT stays LOW during sleep/off (same as normal operation) */
		if (gpio_is_ready_dt(&gps_rtcint_gpio)) {
			gpio_pin_configure_dt(&gps_rtcint_gpio, GPIO_OUTPUT_LOW);
		}
#endif

#if HAS_GPS_VRTC
		LOG_INF("GPS power OFF (%s)", keep_vrtc ?
			"standby — VRTC retained" : "full");
#else
		LOG_INF("GPS power OFF");
#endif
	}
#else
	ARG_UNUSED(keep_vrtc);
#endif
}

/* Drive all GPS power-enable GPIOs LOW for System OFF.
 * Uses gpio_pin_configure_dt() so pins are properly set even if
 * gps_power_control() was never called (GPIO not yet configured). */
void gps_power_off_for_shutdown(void)
{
#if HAS_GPS_POWER_REGULATOR
	if (gps_reg_enabled && device_is_ready(gps_power_reg)) {
		regulator_disable(gps_power_reg);
		gps_reg_enabled = false;
	}
#endif
#if HAS_GPS_POWER_CONTROL
	if (gpio_is_ready_dt(&gps_enable_gpio)) {
		gpio_pin_configure_dt(&gps_enable_gpio, GPIO_OUTPUT_LOW);
	}
#endif
#if HAS_GPS_VRTC
	if (gpio_is_ready_dt(&gps_vrtc_gpio)) {
		gpio_pin_configure_dt(&gps_vrtc_gpio, GPIO_OUTPUT_LOW);
	}
#endif
#if HAS_GPS_RESET
	if (gpio_is_ready_dt(&gps_reset_gpio)) {
		gpio_pin_configure_dt(&gps_reset_gpio, GPIO_OUTPUT_LOW);
	}
#endif
#if HAS_GPS_SLEEP
	if (gpio_is_ready_dt(&gps_sleep_gpio)) {
		gpio_pin_configure_dt(&gps_sleep_gpio, GPIO_OUTPUT_LOW);
	}
#endif
#if HAS_GPS_RTCINT
	if (gpio_is_ready_dt(&gps_rtcint_gpio)) {
		gpio_pin_configure_dt(&gps_rtcint_gpio, GPIO_OUTPUT_LOW);
	}
#endif
#if HAS_GPS_RESETB
	if (gpio_is_ready_dt(&gps_resetb_gpio)) {
		gpio_pin_configure_dt(&gps_resetb_gpio, GPIO_OUTPUT_LOW);
	}
#endif
}

#if HAS_GNSS  /* Resume GNSS-specific code */

/* ========== Software Sleep/Wake (no GPIO required) ==========
 *
 * On boards without dedicated GPS power control (e.g. RAK3401 where the
 * 3V3_S rail is shared with the LoRa FEM), we send vendor-specific UART
 * commands to put the GPS module into low-power mode.
 *
 * Strategy: send BOTH MediaTek and u-blox sleep commands — the module that
 * isn't present simply ignores the bytes it doesn't understand.
 *
 * - MediaTek (e.g. L76B):      $PMTK161,0*28\r\n → standby (~1mA), wake on UART
 * - u-blox ZOE-M8Q (RAK12500): UBX-RXM-PMREQ    → backup  (~7µA), wake on UART
 *
 * Neither reaches a CASIC part (L76K/L76KB/Air530Z): those ignore PMTK, UBX
 * and PCAS12 sleep commands alike — verified on hardware, which is why the
 * boards carrying them duty-cycle with a power GPIO instead. Note also that
 * the RAK1910 is a u-blox MAX-7Q, not an L76K, despite older comments here.
 *
 * Wake: any byte on UART wakes both modules from their low-power modes.
 * After wake, the module resumes outputting NMEA autonomously.
 */

/* HAS_GPS_UART, gps_uart_dev and gps_uart_send are defined near the top of
 * this file (see "GPS Feature Detection") — they are needed by the boot-time
 * module configuration, which runs long before this section. */

/* ========== GNSS UART Suspend/Resume (device PM) ==========
 * nRF UARTE only. An armed UARTE RX holds HFCLK (~0.5-1 mA on nRF52840)
 * even when the GPS module is powered off or silent, so standby/off
 * suspends the UART device and every wake resumes it first.
 *
 * Verified symmetric in uart_nrfx_uarte.c under the still-open modem pipe:
 * suspend saves the RX-interrupt state, STOPRXes, disables the peripheral
 * and applies the sleep pinctrl; resume restores all of it. Other UART
 * drivers (legacy nordic,nrf-uart on RAK4631, ESP32) are deliberately not
 * gated in — their suspend/resume round-trip is unverified and the HFCLK
 * cost is UARTE-specific.
 *
 * Every GPS UART node must carry a sleep pinctrl state (all boards do):
 * without one, suspend fails *after* disabling RX while the PM state stays
 * ACTIVE, so the next resume no-ops with -EALREADY — a dead GPS.
 *
 * REQUIRES GPS POWER CONTROL — do not relax this gate.
 * uarte_pm_suspend() busy-waits for RXTO with no timeout after triggering
 * STOPRX (uart_nrfx_uarte.c, the only unbounded wait in the path). In
 * interrupt-driven mode RX runs on a 1-byte buffer with no ENDRX_STARTRX
 * short, so the receiver stops after every byte until the ISR re-arms it —
 * and suspend disables the ENDRX interrupt *before* STOPRX, removing the
 * re-arm. Land in that window with bytes still arriving and STOPRX hits an
 * already-stopped receiver, no RXTO is generated, and the caller spins
 * forever. It runs on the main thread, so the whole mesh wedges (observed:
 * RAK3401 1W repeater on 1.16.6, CLI answering only "-> busy").
 *
 * Boards with GPIO/regulator power control cut the module before we get
 * here, so the line is genuinely quiet and STOPRX always yields RXTO.
 * Boards without it fall back to gps_software_sleep(), whose PMTK/UBX
 * commands the module may simply ignore (u-blox MAX-7Q on RAK3401 is
 * protocol 14/15; the 16-byte UBX-RXM-PMREQ we send is protocol 23+) —
 * NMEA keeps streaming straight into the suspend. Those boards give up the
 * ~0.5-1 mA HFCLK saving; uptime wins. */
#if HAS_GPS_UART && defined(CONFIG_PM_DEVICE) && \
    (HAS_GPS_POWER_CONTROL || HAS_GPS_POWER_REGULATOR) && \
    DT_NODE_HAS_COMPAT(DT_BUS(DT_NODELABEL(gnss)), nordic_nrf_uarte)
#define HAS_GPS_UART_PM 1
#else
#define HAS_GPS_UART_PM 0
#endif

/* Suspend/resume the GNSS UART. Main thread only (like all GPS power
 * paths — pm_device_action_run() calls the driver synchronously).
 * Ordering: resume BEFORE powering the module / sending the wake byte;
 * suspend AFTER the module is off / sleep commands were sent. */
#if HAS_GPS_UART_PM
static void gps_uart_set_power(bool on)
{
	if (!device_is_ready(gps_uart_dev)) {
		return;
	}
	if (!on) {
		/* Let the GNSS line go quiet before suspending. Every caller
		 * cuts module power (GPS_EN low / reset asserted / regulator
		 * off) immediately before this, but a byte can still be in
		 * flight. Settle so it finishes and the driver's RX ISR re-arms,
		 * leaving the receiver armed-and-idle when the suspend's STOPRX
		 * fires — that state yields RXTO, whereas a just-stopped,
		 * un-rearmed receiver can produce none and (pre-0010) hung the
		 * main thread. ~5 ms comfortably covers one character time at
		 * GNSS baud plus ISR latency; standby happens at most every few
		 * minutes, so the cost is negligible. Backstop: patch 0010
		 * bounds the driver's RXTO wait so a missed RXTO can never hang
		 * us even if a byte still lands in the race window. */
		k_msleep(5);
	}
	int ret = pm_device_action_run(gps_uart_dev,
				       on ? PM_DEVICE_ACTION_RESUME
					  : PM_DEVICE_ACTION_SUSPEND);
	if (ret == 0) {
		LOG_INF("GPS UART %s", on ? "resumed" : "suspended");
	} else if (ret != -EALREADY) {
		LOG_WRN("GPS UART %s failed: %d", on ? "resume" : "suspend", ret);
	}
}
#else
static inline void gps_uart_set_power(bool on) { ARG_UNUSED(on); }
#endif

#if HAS_GPS_UART && !HAS_GPS_POWER_CONTROL && !HAS_GPS_POWER_REGULATOR
/* gps_uart_send() lives near the top of the file — see "GPS Feature
 * Detection". Only the sleep/wake commands below are gated on this board
 * having no hardware GPS power control. */

/* MediaTek parts: $PMTK161,0*28\r\n → enter standby mode
 * Module stops NMEA output and draws ~1mA. Wakes on any UART RX byte.
 * Inert on CASIC parts (L76K and relatives) — they have no such command. */
static const uint8_t pmtk_standby[] = "$PMTK161,0*28\r\n";

/* u-blox ZOE-M8Q: UBX-RXM-PMREQ → enter backup mode
 * UBX frame: B5 62 | 02 41 | 10 00 | payload(16) | CK_A CK_B
 * Payload (protocol 23+, 16 bytes):
 *   version=0, reserved[3]=0,
 *   duration=0x00000000 (infinite),
 *   flags=0x00000006 (backup + force),
 *   wakeupSources=0x00000028 (uartrx bit3 | extint0 bit5)
 *
 * THE WAKE SOURCE BIT IS LOAD-BEARING. wakeupSources bit 3 is uartrx; bit 5
 * is extint0. This frame previously sent 0x20 — extint0 only — with a comment
 * claiming that was "UART RX (bit 5)". It is not. EXTINT is not routed to the
 * WisBlock connector on the RAK12500 (RAK's datasheet: only UART/I2C, 1PPS,
 * RESET, VDD and GND are connected), so the module was told to sleep forever
 * with a wake source that can never be asserted. duration=0 means infinite,
 * so it never came back: no NMEA at any baud, and no I2C either, because
 * backup mode powers down the DDC interface too. Only a physical power cycle
 * recovered it — a reboot does not, since the 3V3_S rail stays up.
 * Confirmed on hardware: reseat -> module answers -> one fix -> first standby
 * -> gone again.
 *
 * 0x28 sets both, so a board that does wire EXTINT keeps that path as well.
 * Module stops all output and draws ~20µA (ZOE-M8Q at 3V, datasheet Table 13;
 * the 15µA hardware-backup figure needs VCC removed entirely). */
static const uint8_t ubx_pmreq_backup[] = {
	0xB5, 0x62,             /* UBX sync chars */
	0x02, 0x41,             /* Class: RXM, ID: PMREQ */
	0x10, 0x00,             /* Length: 16 bytes (little-endian) */
	/* Payload */
	0x00,                   /* version */
	0x00, 0x00, 0x00,       /* reserved1[3] */
	0x00, 0x00, 0x00, 0x00, /* duration: 0 = infinite */
	0x06, 0x00, 0x00, 0x00, /* flags: backup(0x02) | force(0x04) */
	0x28, 0x00, 0x00, 0x00, /* wakeupSources: uartrx(bit3) | extint0(bit5) */
	/* Checksum (Fletcher-8 over class..payload) */
	0x81, 0xEB
};

/* Put GPS module into software sleep (for boards without GPIO power control).
 * Sends both Quectel PMTK and u-blox UBX commands — the wrong one is
 * harmlessly ignored by whichever module is actually connected. */
static void gps_software_sleep(void)
{
	LOG_INF("GPS: Sending software sleep (PMTK + UBX)");

	/* MediaTek standby */
	gps_uart_send(pmtk_standby, sizeof(pmtk_standby) - 1);  /* exclude null terminator */

	/* Small delay between commands — let the first one drain */
	k_msleep(50);

	/* u-blox ZOE-M8Q backup */
	gps_uart_send(ubx_pmreq_backup, sizeof(ubx_pmreq_backup));

	LOG_DBG("GPS: Software sleep commands sent");
}

/* Wake GPS module from software sleep.
 * A single 0xFF byte on UART triggers wake on both Quectel and u-blox.
 * After wake, the module resumes NMEA output within ~100-500ms. */
static void gps_software_wake(void)
{
	LOG_INF("GPS: Sending UART wake byte");
	const uint8_t wake = 0xFF;
	gps_uart_send(&wake, 1);
	/* Give the module time to boot and start NMEA output */
	k_msleep(200);
}
#endif /* HAS_GPS_UART && !HAS_GPS_POWER_CONTROL && !HAS_GPS_POWER_REGULATOR */

/* Acquire-window timeout (ms) for the current phase.
 * - Repeater: fixed 5-min time-sync window.
 * - First acquisition after enable (cold start, no fix yet): a generous but
 *   bounded window so almanac download has time, without pinning the module
 *   on forever when there's no sky. Spent once (first_acquire_used set on the
 *   first standby), after which the node uses the normal duty cycle regardless
 *   of whether a fix was obtained.
 * - All later windows: the normal (warm) acquire timeout. */
static uint32_t gps_acquire_window_ms(void)
{
	if (gps_repeater_mode) {
		return GPS_REPEATER_SYNC_TIMEOUT_MS;
	}
	if (!first_fix_acquired && !first_acquire_used) {
		return gps_first_fix_timeout_ms;
	}
	return gps_acquire_timeout_ms;
}

/* Go to standby and schedule next wake.
 * GPIO power control only — keep VRTC for warm start on T1000-E,
 * FORCE_ON pin LOW for L76K hardware standby. */
static void gps_go_to_standby(void)
{
	/* Unified standby interval for both roles — set from prefs.gps_interval
	 * at boot (companion default 300s, repeater default 48h). Always-on
	 * (interval 0) never reaches here. */
	uint64_t wake_interval = gps_wake_interval_ms;

	LOG_INF("GPS: Going to standby for %llu s%s",
		(unsigned long long)(wake_interval / 1000),
		gps_repeater_mode ? " (repeater time sync)" : "");
	gps_current_state = GPS_STATE_STANDBY;
	consecutive_good_fixes = 0;
	/* The one-time long cold-start window (if any) is now spent — later
	 * wakes use the normal (warm) acquire timeout via gps_acquire_window_ms(). */
	first_acquire_used = true;

	/* Record standby timing for UI (next-wake calculation) */
	standby_start_ms = k_uptime_get();
	standby_interval_ms = wake_interval;

	/* Power down the GPS module.
	 * GPIO boards: hardware power-off (keep VRTC for warm start on T1000-E).
	 * Regulator boards: cut the main rail entirely (both roles). The AXP2101
	 *   VBACKUP charger keeps the receiver's V_BCKP domain alive, so ephemeris/
	 *   RTC survive the cut and re-acquisition is a warm/hot start, not cold.
	 * Other non-GPIO boards: software sleep via UART commands (PMTK + UBX). */
#if HAS_GPS_POWER_CONTROL
	gps_power_control(false, true);
#elif HAS_GPS_POWER_REGULATOR
	gps_power_control(false);
#elif HAS_GPS_UART
	gps_software_sleep();
#endif

	/* Module is off/asleep — release the UART until the next wake
	 * (nRF: drops the HFCLK request held by the armed RX). */
	gps_uart_set_power(false);

	/* NOTE: gnss_configured stays true — L76K retains PCAS settings in
	 * flash across power cycles. Re-running gnss_configure() after GPIO
	 * wake would call modem_chat_run_script() before the chip has booted,
	 * risking a deadlock (modem_chat blocks on system work queue). */

	/* Schedule next wake (event-driven, no polling!) */
	k_work_schedule(&gps_wake_work, K_MSEC(wake_interval));
}

/* Wake GPS and start acquiring.
 * GPIO boards: hardware power-on.
 * Non-GPIO boards: UART wake byte (wakes L76K from standby, ZOE-M8Q from backup).
 * Does NOT call gnss_configure() — constellation/fix-rate settings persist
 * in L76K flash across power cycles. Calling modem_chat_run_script() here
 * would deadlock: the chip needs ~300ms to boot after GPIO power restore,
 * but modem_chat blocks the calling thread waiting for the system work
 * queue which may be processing stale UART data. */
static void gps_start_acquiring(void)
{
	LOG_INF("GPS: Waking for %s", gps_repeater_mode ? "time sync" : "position fix");
	gps_current_state = GPS_STATE_ACQUIRING;
	consecutive_good_fixes = 0;
	gnss_activity_seen_this_cycle = false;

	/* Bring the UART back before the module powers up / the wake byte
	 * goes out, so the first NMEA sentences aren't lost. */
	gps_uart_set_power(true);

#if HAS_GPS_POWER_CONTROL || HAS_GPS_POWER_REGULATOR
	gps_power_control(true);
#elif HAS_GPS_UART
	gps_software_wake();
#endif

	/* Schedule the standby timeout — unless always-on (interval 0), where the
	 * GPS stays in continuous acquisition and never sleeps. Every duty window
	 * is bounded (the first cold-start window is just longer; see
	 * gps_acquire_window_ms). */
	if (gps_duty_cycling()) {
		uint32_t timeout_ms = gps_acquire_window_ms();
		LOG_INF("GPS: Acquire window %u s", timeout_ms / 1000U);
		k_work_schedule(&gps_timeout_work, K_MSEC(timeout_ms));
	} else {
		LOG_INF("GPS: Always-on (continuous, no standby)");
	}
}

/* Work handler: wake GPS for fix.
 * Runs on system work queue — MUST NOT call blocking GNSS APIs directly!
 * modem_chat_run_script() blocks on a semaphore signaled from this same
 * work queue, causing a deadlock. Instead, set a flag and signal the
 * main thread to do the actual wake via gps_process_event(). */
static void gps_wake_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!gps_enabled || gps_current_state != GPS_STATE_STANDBY) {
		return;
	}

	atomic_or(&pending_gps_actions, GPS_ACTION_WAKE);
	if (gps_event_cb) {
		gps_event_cb();
	}
}

/* Work handler: timeout waiting for fix.
 * Runs on system work queue — MUST NOT call blocking GNSS APIs directly!
 * Same deadlock risk as gps_wake_work_fn. Defer to main thread. */
static void gps_timeout_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	if (gps_current_state != GPS_STATE_ACQUIRING) {
		return;
	}

	LOG_WRN("GPS: Timeout after %d/%d fixes, deferring standby to main thread",
		consecutive_good_fixes, GPS_GOOD_FIX_COUNT);

	if (gps_repeater_mode && !gnss_activity_seen_this_cycle) {
		LOG_WRN("GPS: Repeater acquire window had no GNSS callbacks; retrying on next cycle");
	}

	atomic_or(&pending_gps_actions, GPS_ACTION_TIMEOUT);
	if (gps_event_cb) {
		gps_event_cb();
	}
}

/* ========== GPS UART Diagnostics ========== */

/**
 * Dump nRF52840 UARTE0 hardware register state.
 * Reads PSEL (pin select), ENABLE, BAUDRATE, and ERRORSRC directly
 * from the peripheral registers — no assumptions, just facts.
 */
static void gps_uart_dump_hw_state(void)
{
#if defined(CONFIG_SOC_NRF52840)
	NRF_UARTE_Type *uart = NRF_UARTE0;

	uint32_t psel_txd = uart->PSEL.TXD;
	uint32_t psel_rxd = uart->PSEL.RXD;
	uint32_t enable   = uart->ENABLE;
	uint32_t baudrate = uart->BAUDRATE;
	uint32_t errorsrc = uart->ERRORSRC;

	/* PSEL format: bit 31 = CONNECT (0=connected, 1=disconnected),
	 * bits 4:0 = pin, bit 5 = port */
	bool txd_connected = !(psel_txd & (1U << 31));
	bool rxd_connected = !(psel_rxd & (1U << 31));
	uint8_t txd_port = (psel_txd >> 5) & 1;
	uint8_t txd_pin  = psel_txd & 0x1F;
	uint8_t rxd_port = (psel_rxd >> 5) & 1;
	uint8_t rxd_pin  = psel_rxd & 0x1F;

	LOG_INF("UART0 HW state:");
	LOG_INF("  ENABLE=0x%02x (8=enabled)", enable);
	LOG_INF("  PSEL.TXD=0x%08x → P%d.%02d %s",
		psel_txd, txd_port, txd_pin,
		txd_connected ? "CONNECTED" : "DISCONNECTED");
	LOG_INF("  PSEL.RXD=0x%08x → P%d.%02d %s",
		psel_rxd, rxd_port, rxd_pin,
		rxd_connected ? "CONNECTED" : "DISCONNECTED");
	LOG_INF("  BAUDRATE=0x%08x ERRORSRC=0x%x", baudrate, errorsrc);

	/* Clear any error flags */
	if (errorsrc) {
		uart->ERRORSRC = errorsrc;
		LOG_WRN("  UART errors cleared: overrun=%d parity=%d framing=%d break=%d",
			(errorsrc >> 0) & 1, (errorsrc >> 1) & 1,
			(errorsrc >> 2) & 1, (errorsrc >> 3) & 1);
	}
#endif
}

#if HAS_GPS_POWER_CONTROL
/**
 * Log actual GPIO pin states after power-up sequence.
 * Reads back each configured pin to verify the hardware accepted our config.
 */
static void gps_dump_gpio_states(void)
{
	LOG_INF("GPS GPIO states after power-up:");
	if (gpio_is_ready_dt(&gps_enable_gpio)) {
		LOG_INF("  GPS_EN (P1.11): %d", gpio_pin_get_dt(&gps_enable_gpio));
	}
#if HAS_GPS_VRTC
	if (gpio_is_ready_dt(&gps_vrtc_gpio)) {
		LOG_INF("  GPS_VRTC_EN (P0.08): %d", gpio_pin_get_dt(&gps_vrtc_gpio));
	}
#endif
#if HAS_GPS_RESET
	if (gpio_is_ready_dt(&gps_reset_gpio)) {
		LOG_INF("  GPS_RESET (P1.15): %d", gpio_pin_get_dt(&gps_reset_gpio));
	}
#endif
#if HAS_GPS_SLEEP
	if (gpio_is_ready_dt(&gps_sleep_gpio)) {
		LOG_INF("  GPS_SLEEP_INT (P1.12): %d", gpio_pin_get_dt(&gps_sleep_gpio));
	}
#endif
#if HAS_GPS_RTCINT
	if (gpio_is_ready_dt(&gps_rtcint_gpio)) {
		LOG_INF("  GPS_RTC_INT (P0.15): %d", gpio_pin_get_dt(&gps_rtcint_gpio));
	}
#endif
#if HAS_GPS_RESETB
	if (gpio_is_ready_dt(&gps_resetb_gpio)) {
		LOG_INF("  GPS_RESETB (P1.14): %d (INPUT_PULLUP, expect 1)",
			gpio_pin_get_dt(&gps_resetb_gpio));
	}
#endif
}
#endif /* HAS_GPS_POWER_CONTROL */

/* ========== GNSS Init ========== */

static int gnss_init(void)
{
	/* Try to find a GNSS device - prefer chip-specific drivers
	 * (they support constellation config, fix rate, etc.) over
	 * the generic NMEA parser which is passive only.
	 * Power control is done lazily in gps_enable(). */
#if DT_HAS_COMPAT_STATUS_OKAY(quectel_lc76g)
	gnss_dev = DEVICE_DT_GET_ANY(quectel_lc76g);
#elif DT_HAS_COMPAT_STATUS_OKAY(luatos_air530z)
	gnss_dev = DEVICE_DT_GET_ANY(luatos_air530z);
#elif DT_HAS_COMPAT_STATUS_OKAY(quectel_lcx6g)
	gnss_dev = DEVICE_DT_GET_ANY(quectel_lcx6g);
#elif DT_HAS_COMPAT_STATUS_OKAY(u_blox_m8)
	gnss_dev = DEVICE_DT_GET_ANY(u_blox_m8);
#elif DT_HAS_COMPAT_STATUS_OKAY(u_blox_f9p)
	gnss_dev = DEVICE_DT_GET_ANY(u_blox_f9p);
#elif DT_HAS_COMPAT_STATUS_OKAY(gnss_nmea_generic)
	gnss_dev = DEVICE_DT_GET_ANY(gnss_nmea_generic);
#endif

	if (gnss_dev == NULL) {
		LOG_WRN("No GNSS device found in device tree");
		return -ENODEV;
	}

	if (!device_is_ready(gnss_dev)) {
		/* Root cause: GPS transmits NMEA immediately at power-up before
		 * modem_chat opens its DMA pipe. UARTE accumulates overrun/framing
		 * errors, causing modem_pipe_open() to fail and device_init to return
		 * an error.
		 *
		 * Strategy: use UARTE ERRORSRC as a real signal. Wait until errors
		 * appear (GPS is transmitting), clear them, then call device_init.
		 * This avoids arbitrary delays — we act when the hardware tells us
		 * conditions are ready, not after a fixed sleep.
		 *
		 * IMPORTANT: Do NOT use uart_poll_in() — it corrupts nRF52840 UARTE
		 * DMA state and breaks modem_pipe async receive. */
		LOG_INF("GNSS device not ready — waiting for GPS activity on UART");
		gps_power_control(true);

#if HAS_GPS_POWER_CONTROL
		gps_dump_gpio_states();
#endif

#if defined(CONFIG_SOC_NRF52840)
		NRF_UARTE_Type *uart = NRF_UARTE0;

		/* Wait up to 2s for UARTE errors — their presence means the GPS
		 * module is alive and transmitting (ERRORSRC gets set because no
		 * DMA buffer is configured yet). */
		bool gps_active = false;
		for (int t = 0; t < 200; t++) {
			if (uart->ERRORSRC != 0) {
				LOG_INF("GPS UART activity detected after ~%dms "
					"(ERRORSRC=0x%x)", t * 10, uart->ERRORSRC);
				gps_active = true;
				break;
			}
			k_msleep(10);
		}
		if (!gps_active) {
			LOG_WRN("No GPS UART activity within 2s — module may not be "
				"powered or transmitting");
		}
#else
		/* Non-nRF52840: no direct UARTE register access, fall back to
		 * a brief fixed wait for the GPS to start transmitting. */
		k_msleep(500);
#endif

		bool init_ok = false;
		for (int attempt = 0; attempt < 3 && !init_ok; attempt++) {
			/* Clear accumulated UART errors before opening the modem pipe */
			gps_uart_dump_hw_state();

			int ret = device_init(gnss_dev);
			if (ret != 0 && ret != -EALREADY) {
				LOG_WRN("GNSS device_init attempt %d failed: %d",
					attempt + 1, ret);
				/* Small wait for UART to settle, then retry */
				k_msleep(100);
				continue;
			}

			/* Poll for readiness — modem_chat needs a brief moment to
			 * complete pipe setup after device_init returns. */
			for (int t = 0; t < 50; t++) {
				if (device_is_ready(gnss_dev)) {
					LOG_INF("GNSS ready after ~%dms (attempt %d)",
						t * 10, attempt + 1);
					init_ok = true;
					break;
				}
				k_msleep(10);
			}

			if (!init_ok) {
				LOG_WRN("GNSS not ready after attempt %d", attempt + 1);
			}
		}

		if (!init_ok) {
			LOG_ERR("GNSS device failed to initialize");
			gps_uart_dump_hw_state();
			return -ENODEV;
		}
	}

#ifdef CONFIG_PM_DEVICE
	/* Some upstream GNSS drivers (gnss-nmea-generic) start suspended under
	 * CONFIG_PM_DEVICE and never open their modem pipe until resumed — no
	 * NMEA would ever flow (the old "PM broke GPS" trap). Resume once
	 * here: main thread at boot, the one safe context for the modem_chat
	 * scripts a resume may run. PM-less drivers (luatos,air530z) return
	 * -ENOSYS. Retried like device_init above — opening the pipe while
	 * the module is mid-sentence can fail transiently. */
	int pm_ret = pm_device_action_run(gnss_dev, PM_DEVICE_ACTION_RESUME);
	for (int attempt = 1; pm_ret != 0 && pm_ret != -EALREADY &&
	     pm_ret != -ENOSYS && attempt < 3; attempt++) {
		LOG_WRN("GNSS PM resume failed (%d), retrying", pm_ret);
		k_msleep(100);
		pm_ret = pm_device_action_run(gnss_dev, PM_DEVICE_ACTION_RESUME);
	}
	if (pm_ret != 0 && pm_ret != -EALREADY && pm_ret != -ENOSYS) {
		LOG_ERR("GNSS PM resume failed: %d", pm_ret);
		return -ENODEV;
	}
#endif

	LOG_INF("GNSS device %s initialized", gnss_dev->name);
	gps_available = true;
	return 0;
}
#endif /* HAS_GNSS */

/* ========== Public API ========== */

int gps_manager_init(void)
{
#if HAS_GNSS
	gnss_init();

	/* Configure constellations + fix rate NOW while chip is powered
	 * and the modem pipe is open (driver init already ran).
	 * This is the ONLY safe place to call modem_chat_run_script() —
	 * after power cycles the chip needs ~300ms boot time and calling
	 * modem_chat from the main thread risks deadlock. L76K retains
	 * PCAS settings in flash, so one-time config at boot is enough. */
	gnss_configure();
#endif
	return 0;
}

bool gps_is_available(void)
{
#if HAS_GNSS
	return gps_available;
#else
	return false;
#endif
}

bool gps_is_enabled(void)
{
#if HAS_GNSS
	return gps_enabled;
#else
	return false;
#endif
}

void gps_ensure_power_state(bool should_be_enabled)
{
#if HAS_GNSS
	if (!gps_available) {
		return;
	}

	/* At boot, GPS hardware is powered (bootloader/pull-up).
	 * If it should be disabled, explicitly power it off now. */
	if (!should_be_enabled) {
		LOG_INF("GPS: Powering off at boot (disabled in prefs)");
		gps_power_control(false);
		/* GPS stays off — release the UART too. Without this, the RX
		 * armed at driver init would hold HFCLK for the entire uptime
		 * of every GPS-disabled node. */
		gps_uart_set_power(false);
		gps_current_state = GPS_STATE_OFF;
	}
#else
	ARG_UNUSED(should_be_enabled);
#endif
}

void gps_set_repeater_mode(bool repeater)
{
#if HAS_GNSS
	if (!gps_available) {
		return;
	}

	gps_repeater_mode = repeater;

	if (repeater) {
		LOG_INF("GPS: Repeater mode - starting initial time sync, then every 48h");

		gps_enabled = true;  /* Logically enabled */

		/* Start acquiring immediately for initial time sync at boot.
		 * GPS hardware is already powered from bootloader, so we just
		 * start the acquisition state machine. */
		gps_start_acquiring();
	} else {
		LOG_INF("GPS: Companion mode");
	}
#else
	ARG_UNUSED(repeater);
#endif
}

void gps_enable(bool enable)
{
#if HAS_GNSS
	if (!gps_available) {
		LOG_WRN("GPS not available");
		return;
	}

	if (enable == gps_enabled) {
		return;
	}

	gps_enabled = enable;

	if (enable) {
		LOG_INF("GPS enabled - starting acquisition");

		/* Block SoC light sleep for as long as the module is powered.
		 * The GNSS UART is not a wake source, so a sleeping SoC drops
		 * inbound NMEA outright — sentences would be lost mid-stream and
		 * a fix would never converge. Balanced by the put in the disable
		 * branch; the early return above keeps the pair 1:1, and under a
		 * GPS duty cycle the lock is only held during the awake phase.
		 * Compiles to nothing without CONFIG_PM. */
		zc_pm_block_sleep();

		/* Start acquiring immediately (no delay for first wake) */
		gps_current_state = GPS_STATE_ACQUIRING;
		consecutive_good_fixes = 0;

		/* Resume the GNSS UART first so no NMEA is lost at power-on
		 * (it may be suspended from a boot-with-GPS-off or a prior
		 * disable). */
		gps_uart_set_power(true);

		/* Power on GPS - uses lazy GPIO init */
		gps_power_control(true);

		/* gnss_configure() runs once at boot (see gps_manager_init path).
		 * L76K retains PCAS settings in flash across power cycles.
		 * Do NOT call modem_chat_run_script() here — the chip needs
		 * ~300ms to boot after GPIO power restore and calling it
		 * immediately deadlocks the main thread. */
		gps_diag_maybe_reconfigure();

		/* Bounded first-acquisition window, then the normal duty cycle —
		 * unless always-on (interval 0), where GPS never sleeps. */
		if (gps_duty_cycling()) {
			uint32_t timeout_ms = gps_acquire_window_ms();
			LOG_INF("GPS: Acquire window %u s", timeout_ms / 1000U);
			k_work_schedule(&gps_timeout_work, K_MSEC(timeout_ms));
		} else {
			LOG_INF("GPS: Always-on (continuous, no standby)");
		}
	} else {
		LOG_INF("GPS disabled - canceling timers and powering off");

		/* Matches the block taken in the enable branch. */
		zc_pm_unblock_sleep();

		/* Cancel any pending work */
		k_work_cancel_delayable(&gps_wake_work);
		k_work_cancel_delayable(&gps_timeout_work);

		/* Power off GPS — warm standby if VRTC available (Arduino sleep_gps),
		 * full power off otherwise. Warm standby preserves ephemeris/RTC
		 * in AG3335 backup RAM for fast re-acquisition (1-8s vs 15-45s). */
#if HAS_GPS_VRTC
		gps_power_control(false, true);  /* Warm standby — keep VRTC */
#else
		gps_power_control(false);        /* No VRTC — full power off */
#endif

		/* GPS is off until re-enabled — release the UART. */
		gps_uart_set_power(false);

		gps_current_state = GPS_STATE_OFF;
		consecutive_good_fixes = 0;

		/* Zero the stale satellite count so a re-enable doesn't briefly
		 * report a live fix (e.g. joystick UI showing "3D FIX") off old
		 * data before any new NMEA sentence arrives. lat/lon/valid are
		 * deliberately left alone — telemetry/UI "last known position"
		 * reads (gps_get_position) intentionally survive an on/off
		 * toggle; only the live fix-quality indicator resets. */
		k_mutex_lock(&gps_mutex, K_FOREVER);
		current_pos.satellites = 0;
		last_gnss_satellites = 0;
		last_gnss_visible_satellites = 0;
#if IS_ENABLED(CONFIG_GNSS_SATELLITES)
		memset(gnss_visible_by_system, 0, sizeof(gnss_visible_by_system));
#endif
		k_mutex_unlock(&gps_mutex);

		/* Clear first-fix flags so the next enable gets a fresh long
		 * first-acquisition window again — the user explicitly toggled GPS
		 * expecting it to try hard for a fix. */
		first_fix_acquired = false;
		first_acquire_used = false;

		/* Clear time sync flag - time will drift, allow phone sync again */
		gps_time_synced = false;
	}

	/* Notify callback (for persistence in main.cpp) */
	if (gps_enable_cb) {
		gps_enable_cb(enable);
	}
#else
	ARG_UNUSED(enable);
#endif
}

void gps_get_position(struct gps_position *pos)
{
#if HAS_GNSS
	k_mutex_lock(&gps_mutex, K_FOREVER);
	*pos = current_pos;
	k_mutex_unlock(&gps_mutex);
#else
	memset(pos, 0, sizeof(*pos));
#endif
}

uint32_t gps_get_poll_interval_sec(void)
{
#if HAS_GNSS
	return gps_wake_interval_ms / 1000U;
#else
	return CONFIG_ZEPHCORE_GPS_POLL_INTERVAL_SEC;
#endif
}

void gps_set_poll_interval_sec(uint32_t interval)
{
#if HAS_GNSS
	/* 0 = always-on (no standby); otherwise floor 10s. Cap at 1 week — sane
	 * for time-sync and safely below the interval*1000 uint32 overflow (~49d). */
	if (interval != 0 && interval < 10) interval = 10;
	if (interval > 604800) interval = 604800;
	gps_wake_interval_ms = interval * 1000U;
	LOG_INF("GPS poll interval set to %u seconds%s", interval,
		interval == 0 ? " (always on)" : "");

	/* Live re-arm so a runtime change takes effect without a reboot. */
	if (!gps_enabled) {
		return;
	}
	if (gps_current_state == GPS_STATE_ACQUIRING) {
		if (interval == 0) {
			/* Switch to always-on: drop the standby timeout so it won't sleep. */
			k_work_cancel_delayable(&gps_timeout_work);
		} else if (!k_work_delayable_is_pending(&gps_timeout_work)) {
			/* Was always-on: arm a timeout so it starts duty cycling. */
			k_work_reschedule(&gps_timeout_work, K_MSEC(gps_acquire_window_ms()));
		}
	} else if (gps_current_state == GPS_STATE_STANDBY) {
		k_work_cancel_delayable(&gps_wake_work);
		if (interval == 0) {
			/* Wake now and stay on. */
			atomic_or(&pending_gps_actions, GPS_ACTION_WAKE);
			if (gps_event_cb) {
				gps_event_cb();
			}
		} else {
			standby_interval_ms = gps_wake_interval_ms;
			k_work_reschedule(&gps_wake_work, K_MSEC(gps_wake_interval_ms));
		}
	}
#else
	ARG_UNUSED(interval);
#endif
}

int64_t gps_get_utc_time(void)
{
#if HAS_GNSS
	k_mutex_lock(&gps_mutex, K_FOREVER);
	if (!current_pos.valid) {
		k_mutex_unlock(&gps_mutex);
		return 0;
	}

	struct gnss_time t = current_utc;
	k_mutex_unlock(&gps_mutex);

	/* Defensive: the date math below indexes month_days[m] for m < t.month.
	 * t.month is a uint8_t straight from the GNSS driver — bound it (and the
	 * day) so a driver that doesn't range-check (the NMEA parser does; binary
	 * UBX/chip drivers are not all verified) can't drive an OOB read of
	 * month_days[13] or a garbage RTC set. */
	if (t.month < 1 || t.month > 12 || t.month_day < 1 || t.month_day > 31) {
		return 0;
	}

	int year = 2000 + t.century_year;
	int days = 0;

	for (int y = 1970; y < year; y++) {
		days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
	}

	static const int month_days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	for (int m = 1; m < t.month; m++) {
		days += month_days[m];
		if (m == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
			days++;
		}
	}

	days += t.month_day - 1;

	int64_t timestamp = (int64_t)days * 86400;
	timestamp += t.hour * 3600;
	timestamp += t.minute * 60;
	timestamp += t.millisecond / 1000;

	return timestamp;
#else
	return 0;
#endif
}

bool gps_has_time_sync(void)
{
#if HAS_GNSS
	/* Returns true if GPS has recently synced the RTC.
	 * Expires after 2 hours without a fix so the phone can re-sync
	 * (e.g. node moved indoors, GPS lost sky, RTC drifting). */
	if (!gps_time_synced) {
		return false;
	}
	int64_t age_ms = k_uptime_get() - last_fix_uptime_ms;

	if (age_ms > (2 * 60 * 60 * 1000LL)) {
		gps_time_synced = false;
		return false;
	}
	return true;
#else
	return false;
#endif
}

bool gps_get_last_known_position(struct gps_position *pos)
{
#if HAS_GNSS
	k_mutex_lock(&gps_mutex, K_FOREVER);
	if (current_pos.valid) {
		*pos = current_pos;
		k_mutex_unlock(&gps_mutex);
		return true;
	}
	k_mutex_unlock(&gps_mutex);
#endif
	memset(pos, 0, sizeof(*pos));
	return false;
}

void gps_request_fresh_fix(void)
{
#if HAS_GNSS
	if (!gps_available || !gps_enabled) {
		return;
	}

	if (gps_current_state == GPS_STATE_STANDBY) {
		LOG_INF("GPS: Fresh fix requested, waking early");
		/* Cancel scheduled wake and wake immediately */
		k_work_cancel_delayable(&gps_wake_work);
		gps_start_acquiring();
	} else if (gps_current_state == GPS_STATE_ACQUIRING) {
		/* Already acquiring — reschedule the timeout so the caller's
		 * fresh-fix request gets a full window from now. Otherwise, a
		 * telemetry request that arrives 25s into a 30s acquire window
		 * only has 5s left, which in marginal signal usually means the
		 * chip goes to standby before producing a fix the requester
		 * could use. Each duty phase has a bounded window
		 * (gps_acquire_window_ms); in always-on there's no timeout to extend
		 * (GPS is continuously acquiring and current_pos is always fresh). */
		if (gps_duty_cycling()) {
			uint32_t timeout_ms = gps_acquire_window_ms();
			LOG_INF("GPS: Fresh fix requested, extending acquire timeout to %u s",
				timeout_ms / 1000U);
			k_work_reschedule(&gps_timeout_work, K_MSEC(timeout_ms));
		}
	}
#endif
}

void gps_set_diag(bool on)
{
	gps_diag_on = on;
#if HAS_GNSS
	if (on) {
		/* Re-arm configuration so the next GPS enable ("gps off" then
		 * "gps on") actually re-runs it and refreshes the record. */
		gnss_configured = false;
	}
#endif
}

bool gps_get_diag(void)
{
	return gps_diag_on;
}

void gps_get_diag_report(char *buf, size_t len)
{
#if HAS_GNSS
	static const char *const path_str[] = { "never", "api", "uart", "blind" };
	uint8_t path = gps_cfg_diag.path;
	const char *pname = (path < ARRAY_SIZE(path_str)) ? path_str[path] : "?";

	if (path == GPS_CFG_NEVER) {
		snprintf(buf, len, "diag=%s cfg=never-run (enable, then 'gps off'/'gps on')",
			 gps_diag_on ? "on" : "off");
		return;
	}

	uint32_t age_s = (uint32_t)((k_uptime_get() - gps_cfg_diag.at_ms) / 1000);
	size_t n = (size_t)snprintf(buf, len, "diag=%s cfg=%s age=%us",
				    gps_diag_on ? "on" : "off", pname, age_s);
	if (n >= len) {
		return;
	}

	/* Sentences parsed from the module. Print this before anything else
	 * that could be misinterpreted — rx=0 makes every other field moot. */
	if (zephcore_gnss_rx_count != NULL) {
		n += (size_t)snprintf(buf + n, len - n, " rx=%u",
				      (unsigned)zephcore_gnss_rx_count());
		if (n >= len) {
			return;
		}
	}

	/* Module identity, when the driver could obtain it. Present means the
	 * module answered us, i.e. the TX path is good; absent on a driver that
	 * asks is a strong hint the module never hears our configuration. */
	if (zephcore_gnss_version_get != NULL) {
		char ver[40];
		if (zephcore_gnss_version_get(ver, sizeof(ver)) > 0) {
			n += (size_t)snprintf(buf + n, len - n, " mod=%s", ver);
		} else {
			n += (size_t)snprintf(buf + n, len - n, " mod=no-reply");
		}
		if (n >= len) {
			return;
		}
	}

	if (path == GPS_CFG_UART) {
		/* Bytes actually clocked out to the module. Note these are sent
		 * blind — this proves transmission, not acceptance. Constellation
		 * tallies below are the acceptance evidence. */
		n += (size_t)snprintf(buf + n, len - n, " sent=%u/%uB",
				      gps_cfg_diag.cmds, gps_cfg_diag.bytes);
	} else if (path == GPS_CFG_API) {
		n += (size_t)snprintf(buf + n, len - n, " sys_ret=%d rate_ret=%d",
				      gps_cfg_diag.api_ret, gps_cfg_diag.rate_ret);
	}
	if (n >= len) {
		return;
	}

#ifdef CONFIG_ZEPHCORE_GPS_SAT_DIAG
	k_mutex_lock(&gps_mutex, K_FOREVER);
	/* Age out constellations that have stopped reporting, so a stale count
	 * is never presented as current. */
	int64_t now_ms = k_uptime_get();
	uint8_t shown[5];
	for (int i = 0; i < 5; i++) {
		shown[i] = ((now_ms - sat_seen_ms[i]) > SAT_TALLY_STALE_MS) ? 0 : sat_count[i];
	}
	k_mutex_unlock(&gps_mutex);
	snprintf(buf + n, len - n, " sys=G%u/R%u/E%u/B%u/?%u",
		 shown[0], shown[1], shown[2], shown[3], shown[4]);
#else
	snprintf(buf + n, len - n, " (build without GPS_SAT_DIAG: no per-constellation proof)");
#endif
#else
	ARG_UNUSED(gps_diag_on);
	snprintf(buf, len, "no GNSS on this board");
#endif
}

void gps_get_state_info(struct gps_state_info *info)
{
	memset(info, 0, sizeof(*info));
#if HAS_GNSS
	info->state = (uint8_t)gps_current_state;
	info->satellites = last_gnss_satellites;
	info->visible_satellites = last_gnss_visible_satellites;

	if (last_fix_uptime_ms > 0) {
		/* Seconds since last validated fix */
		info->last_fix_age_s = (uint32_t)((k_uptime_get() - last_fix_uptime_ms) / 1000);
	} else {
		info->last_fix_age_s = UINT32_MAX;  /* No fix yet */
	}

	if (gps_current_state == GPS_STATE_STANDBY && standby_interval_ms > 0) {
		int64_t wake_at = standby_start_ms + (int64_t)standby_interval_ms;
		int64_t remaining = wake_at - k_uptime_get();

		info->next_search_s = (remaining > 0) ? (uint32_t)(remaining / 1000) : 0;
	} else if (gps_current_state == GPS_STATE_ACQUIRING) {
		info->next_search_s = 0;  /* Searching right now */
	}

#ifdef CONFIG_ZEPHCORE_GPS_SAT_DIAG
	/* Same staleness rule as the diag report — never present a count for a
	 * constellation that has stopped reporting. */
	k_mutex_lock(&gps_mutex, K_FOREVER);
	int64_t sat_now = k_uptime_get();
	uint8_t *dst[5] = { &info->sats_gps, &info->sats_glonass, &info->sats_galileo,
			    &info->sats_beidou, &info->sats_other };
	for (int i = 0; i < 5; i++) {
		*dst[i] = ((sat_now - sat_seen_ms[i]) > SAT_TALLY_STALE_MS) ? 0 : sat_count[i];
	}
	k_mutex_unlock(&gps_mutex);
#endif
#endif
}

/* Process pending GPS state transitions — called from main thread.
 * Work handlers on the system work queue set flags + signal the main
 * thread via gps_event_cb(). The main thread then calls this function,
 * which safely executes blocking GNSS configuration (modem_chat_run_script
 * blocks on a semaphore signaled from the system work queue — calling it
 * FROM the work queue deadlocks). */
void gps_process_event(void)
{
#if HAS_GNSS
	uint32_t actions = (uint32_t)atomic_clear(&pending_gps_actions);

	if (actions == 0) {
		return;
	}

	/* Wake takes priority — if both wake and timeout/fix-done are pending
	 * (shouldn't happen, but be safe), wake wins. */
	if (actions & GPS_ACTION_WAKE) {
		if (gps_enabled && gps_current_state == GPS_STATE_STANDBY) {
			gps_start_acquiring();
		}
	} else if (actions & (GPS_ACTION_TIMEOUT | GPS_ACTION_FIX_DONE)) {
		if (gps_current_state == GPS_STATE_ACQUIRING) {
			gps_go_to_standby();
		}
	}
#endif
}
