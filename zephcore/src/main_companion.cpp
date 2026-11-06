/*
 * SPDX-License-Identifier: MIT
 * ZephCore companion: composition root and mesh event loop.
 *
 * Transports: adapters/ble/ZephyrBLE.cpp, adapters/usb/ZephyrCompanionUSB.cpp.
 * UI to mesh actions: helpers/ui/ui_mesh_actions.cpp.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zephcore_main, CONFIG_ZEPHCORE_MAIN_LOG_LEVEL);
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <ZephyrDataStore.h>
#include <adapters/clock/ZephyrRTCClock.h>
#include <adapters/clock/ZephyrRTCDiscover.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/hwinfo.h>
#include <helpers/boot_info.h>
#include <helpers/LoopWakeStats.h>
#include "mesh_events.h"
#include <zephyr/sys/reboot.h>
#include <ZephyrSensorManager.h>
#if defined(CONFIG_BOARD_THINKNODE_M3) || defined(CONFIG_BOARD_T1000_E)
#include <FallDetector.h>
#define ZEPHCORE_FALL_DETECTOR 1
#endif
#include <helpers/time_sync.h>
#include <helpers/LocalCLIHelp.h>
#include "ui_task.h"
#include "ui_radio_state.h"
#include "ui_mesh_actions.h"
#include <helpers/ui/ui_timezone.h>
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DISPLAY)
#include "display.h"
#endif
#include "oled_power.h"
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DISPLAY) && IS_ENABLED(CONFIG_ZEPHCORE_UI_DESIGN_BUTTON)
#include "ui_pages.h"
#endif
#include "led_gate.h"
#include "buzzer_gate.h"
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
#include "buzzer.h"
#endif

/* The companion transports (BLE, USB/UART, TCP) as upstream
 * BaseSerialInterfaces; also defines ZEPHCORE_USB_STACK. */
#include <CompanionInterfaces.h>
#include <helpers/MultiSerialInterface.h>
#include <app/CompanionSerial.h>
#if IS_ENABLED(CONFIG_ZEPHCORE_COMPANION_WIFI)
#include <app/CompanionWifi.h>
#endif

#if ZEPHCORE_USB_STACK
#include <ZephyrUSBCDC.h>
#endif

#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DESIGN_JOYSTICK)
#include <joystick_ui_task.h>
#include <joystick_ui_hooks.h>
#endif

/* Radio + mesh includes (shared header selects LR1110 or SX126x) */
#include <src/RadioIncludes.h>
#ifdef ZEPHCORE_LORA
#include <app/CompanionMesh.h>
#include <helpers/CommonCLI.h>
#include <helpers/CompanionCLIAudio.h>
#include <helpers/CompanionSOSPolicy.h>
#include <helpers/GpsSwitchPolicy.h>
#include <helpers/TrackingMessage.h>
#include <helpers/GeoFence.h>
#include <helpers/pm_sleep_guard.h>
#include <helpers/StatsFormatHelper.h>
#include <app/CompanionCLI.h>
#include <helpers/battery_curve.h>
#include <zephyr_poweroff.h>
#include <app/PowerPolicy.h>
#endif

#if IS_ENABLED(CONFIG_ZEPHCORE_PACKET_LOGGING)
/* helpers/PacketLog.h hook, a no-op here: packet_logging.conf is for the
 * repeater, room server and observer consoles. A companion's USB port carries
 * the binary app protocol, and its text CLI echoes through its own TX ring
 * (adapters/usb/ZephyrCompanionUSB.cpp). */
extern "C" void zc_console_line_start(void) {}
#endif

/* Without this a BLE controller assert freezes the CPU at top IRQ priority
 * with no output; log the location and reboot. */
#if IS_ENABLED(CONFIG_BT_CTLR_ASSERT_HANDLER)
extern "C" void bt_ctlr_assert_handle(char *file, uint32_t line)
{
	LOG_ERR("!!! BLE CONTROLLER ASSERT: %s:%u !!!", file ? file : "?", line);
	k_sleep(K_MSEC(100));  /* let the log drain */
	sys_reboot(SYS_REBOOT_COLD);
}
#endif

/* The mesh thread blocks on these; ISRs and callbacks post them. The shared
 * bits are in mesh_events.h; these are the companion's own. */
#define MESH_EVENT_UI_ACTION     BIT(MESH_EVENT_ROLE_BASE)      /* Button action from UI (deferred to mesh thread) */
#define MESH_EVENT_JOYSTICK_LOOP BIT(MESH_EVENT_ROLE_BASE + 1)  /* Run the joystick UI loop (on input, notify or its timers) */
#define MESH_EVENT_PREFS_DIRTY   BIT(MESH_EVENT_ROLE_BASE + 2)  /* Prefs mutated off-main; main flushes to flash */
#define MESH_EVENT_CONTACT_ITER  BIT(MESH_EVENT_ROLE_BASE + 3)  /* Continue contact-dump iteration on main thread */
#define MESH_EVENT_LINK          BIT(MESH_EVENT_ROLE_BASE + 4)  /* A transport connected or disconnected */
#define MESH_EVENT_PERSIST       BIT(MESH_EVENT_ROLE_BASE + 5)  /* Flush deferred writes: a reboot/power-off waits on it */
#define MESH_EVENT_CUSTOM_TIMER  BIT(MESH_EVENT_ROLE_BASE + 6)
#define MESH_EVENT_FALL_DETECTED BIT(MESH_EVENT_ROLE_BASE + 7)
#define MESH_EVENT_FALL_PREALERT BIT(MESH_EVENT_ROLE_BASE + 8)
#define MESH_EVENT_GPS_FIX       BIT(MESH_EVENT_ROLE_BASE + 9)
#define MESH_EVENT_MANUAL_SHUTDOWN BIT(MESH_EVENT_ROLE_BASE + 10)

#ifdef ZEPHCORE_LORA
static void save_prefs_to_flash(void);
static void power_policy_tick(void);
static void companion_sos_process(void);
static void companion_sos_tx_done(void);
static void companion_tracking_odometer_fix(void);
static void companion_home_save_process(void);
static void companion_nav_fix_process(void);
static void companion_geo_fix_process(void);
static void companion_gps_user_left(void);
static void companion_nav_link_check(void);
static void companion_tracking_process(void);
static uint32_t companion_custom_ms_until_next(void);
static void companion_fall_detected(void);
static void companion_fall_prealert(void);
#endif

#define MESH_EVENT_BASE          (MESH_EVENT_LORA_RX | MESH_EVENT_LORA_TX_DONE | \
	MESH_EVENT_BLE_RX | MESH_EVENT_HOUSEKEEPING | MESH_EVENT_UI_ACTION |  \
	MESH_EVENT_GPS_ACTION | MESH_EVENT_TX_DRAIN | MESH_EVENT_PREFS_DIRTY | \
	MESH_EVENT_CONTACT_ITER | MESH_EVENT_LINK | MESH_EVENT_PERSIST | \
	MESH_EVENT_CUSTOM_TIMER | MESH_EVENT_FALL_DETECTED | \
	MESH_EVENT_FALL_PREALERT | MESH_EVENT_GPS_FIX | MESH_EVENT_MANUAL_SHUTDOWN)
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DESIGN_JOYSTICK)
#define MESH_EVENT_ALL           (MESH_EVENT_BASE | MESH_EVENT_JOYSTICK_LOOP)
#else
#define MESH_EVENT_ALL           MESH_EVENT_BASE
#endif

#define HOUSEKEEPING_INTERVAL_MS CONFIG_ZEPHCORE_HOUSEKEEPING_INTERVAL_MS

static struct k_event mesh_events;

static void process_companion_rx(void);   /* runs on MAIN thread (see link_on_rx) */
static void run_contact_iteration(void);  /* runs on MAIN thread (see MESH_EVENT_CONTACT_ITER) */
static void housekeeping_timer_fn(struct k_timer *timer);
static void custom_timer_fn(struct k_timer *timer);
#if ZEPHCORE_USB_STACK
static void companion_cli_run(const char *line);  /* main-thread text-CLI exec */
#endif

#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DESIGN_JOYSTICK)
static JoystickUITask joystick_ui_task;

static void joystick_signal_refresh(void)
{
	k_event_post(&mesh_events, MESH_EVENT_JOYSTICK_LOOP);
}

static void joystick_signal_tx(void)
{
	k_event_post(&mesh_events, MESH_EVENT_TX_DRAIN);
}
#endif

/* Threading rule for everything below: the mesh state (packet pool,
 * dispatcher, contact table) belongs to the MAIN thread. Transport callbacks
 * run on sysworkq or in ISR context, so they only queue and post an event;
 * the work happens in the event loop. Breaking this caused the stuck
 * "Sending..." bug (a reply parsed on sysworkq raced loop()). */

#if ZEPHCORE_USB_STACK
/* Completed USB text-CLI lines, drained on MESH_EVENT_BLE_RX. */
#define CLI_LINE_BUF_SIZE 256
struct companion_cli_line { char buf[CLI_LINE_BUF_SIZE]; };
K_MSGQ_DEFINE(companion_cli_queue, sizeof(struct companion_cli_line), 4, 4);
#endif

K_TIMER_DEFINE(housekeeping_timer, housekeeping_timer_fn, NULL);
K_TIMER_DEFINE(custom_timer, custom_timer_fn, NULL);

#ifdef ZEPHCORE_LORA
static CompanionMesh *companion_mesh_ptr;

/* The thread running mesh_event_loop(), once it is running. */
static k_tid_t mesh_thread;
static K_SEM_DEFINE(persist_done, 0, 1);
static atomic_t manual_shutdown_requested;

static void manual_shutdown_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	zephcore_shutdown_reason_save(ZC_SHUTDOWN_USER);
	zephcore_power_off();
}

static K_WORK_DELAYABLE_DEFINE(manual_shutdown_work, manual_shutdown_work_fn);

extern "C" bool ui_request_manual_shutdown(void)
{
	if (!mesh_thread) return false;
	if (atomic_set(&manual_shutdown_requested, 1) == 0) {
		k_event_post(&mesh_events, MESH_EVENT_MANUAL_SHUTDOWN);
	}
	return true;
}

/* Bounds a reboot/power-off requested off the main thread. A full contacts
 * rewrite is a second or two on internal flash; past this the device goes
 * down anyway rather than hang on a wedged main thread. */
#define PERSIST_WAIT_MS 5000

/* Main thread: the deferred contacts/channels writes, and a prefs save still
 * waiting in the event mask. */
static void persist_pending_writes(void)
{
	if (k_event_clear(&mesh_events, MESH_EVENT_PREFS_DIRTY) & MESH_EVENT_PREFS_DIRTY) {
		save_prefs_to_flash();
	}
	companion_mesh_ptr->flushPendingWrites();
}

/* zephyr_poweroff.h: every clean reboot and power-off path calls this. The
 * lazy contacts deadline is long (CompanionMesh.h), so a clean shutdown that
 * skipped it would lose the last hour of contact updates. */
extern "C" void zephcore_persist_before_off(void)
{
	if (!companion_mesh_ptr) {
		return;
	}
	if (!mesh_thread || k_current_get() == mesh_thread) {
		persist_pending_writes();
		return;
	}
	/* The mesh state belongs to the main thread: hand the flush over. */
	k_sem_reset(&persist_done);
	k_event_post(&mesh_events, MESH_EVENT_PERSIST);
	if (k_sem_take(&persist_done, K_MSEC(PERSIST_WAIT_MS)) != 0) {
		LOG_WRN("persist before off: main thread did not flush in %d ms",
			PERSIST_WAIT_MS);
	}
}
#endif

/* ========== Companion transports ==========
 * As upstream's companion_radio/main.cpp: every transport this build has is a
 * BaseSerialInterface in one MultiSerialInterface. Every connected one gets
 * every frame; a command from any of them is served. */
static MultiSerialInterface interface_manager;
static CompanionSerial companion_serial(interface_manager);
#if IS_ENABLED(CONFIG_BT)
static ZephyrBLEInterface ble_interface;
#endif
#if ZEPHCORE_USB_STACK
static ZephyrSerialInterface usb_interface;
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_TRANSPORT_TCP)
static ZephyrTcpInterface tcp_interface;
#endif

/* The transports' callbacks run on their own threads (BT, sysworkq, ISR, the
 * TCP listener), so they only post; the work happens in the event loop. */
static void link_on_rx(void)
{
	k_event_post(&mesh_events, MESH_EVENT_BLE_RX);
}

/* A transport's TX drained: resume the contact dump and the TX drain. */
static void link_on_tx_idle(void)
{
	k_event_post(&mesh_events, MESH_EVENT_CONTACT_ITER);
	k_event_post(&mesh_events, MESH_EVENT_TX_DRAIN);
}

/* Counted, so the loop sees every edge even when several coalesce into one
 * MESH_EVENT_LINK. */
static atomic_t link_ups;
static atomic_t link_downs;

static void link_on_connected(void)
{
	atomic_inc(&link_ups);
	k_event_post(&mesh_events, MESH_EVENT_LINK);
}

static void link_on_disconnected(void)
{
	atomic_inc(&link_downs);
	k_event_post(&mesh_events, MESH_EVENT_LINK);
}

static const struct companion_link_cbs link_cbs = {
	.on_rx = link_on_rx,
	.on_tx_idle = link_on_tx_idle,
	.on_connected = link_on_connected,
	.on_disconnected = link_on_disconnected,
};

#if IS_ENABLED(CONFIG_BT)
#if IS_ENABLED(CONFIG_ZEPHCORE_BLE_DFU)
static void ble_on_dfu_request(void)
{
	mesh_reboot_to_ota_dfu();  /* GPREGRET 0xA8 + reset; never returns */
}
#endif

static const struct ble_callbacks ble_cbs = {
	.link = link_cbs,
#if IS_ENABLED(CONFIG_ZEPHCORE_BLE_DFU)
	.on_dfu_request = ble_on_dfu_request,
#endif
};
#endif

/* The last client went away: drop the contact dump (a stale
 * PACKET_CONTACT_END would confuse the next session), keep the un-ACKed
 * message queued for re-send, free an abandoned sign buffer, and drop a held
 * reply nobody will read. Not while another transport is still connected:
 * it shares the dump and the sync. */
static void companion_session_cleanup(void)
{
	companion_serial.drop();
#ifdef ZEPHCORE_LORA
	companion_mesh_ptr->cancelContactIterator();
	companion_mesh_ptr->cancelSyncPending();
	companion_mesh_ptr->cleanupSignState();
#endif
}

/* Which transports have a client right now, for the display (UI_LINK_*). */
static uint8_t link_transports(void)
{
	uint8_t mask = 0;

#if IS_ENABLED(CONFIG_BT)
	if (zephcore_ble_is_connected()) {
		mask |= UI_LINK_BLE;
	}
#endif
#if ZEPHCORE_USB_STACK
	if (zephcore_usb_companion_link_up()) {
		mask |= UI_LINK_USB;
	}
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_TRANSPORT_TCP)
	if (tcp_companion_is_connected()) {
		mask |= UI_LINK_TCP;
	}
#endif
	return mask;
}

/* MESH_EVENT_LINK: a transport (USB and TCP sessions included) connected or
 * disconnected. The app-connected events stay transport-agnostic (they gate
 * buzzer, LED and unread handling); the mask says which transport it is. */
static void process_link_change(void)
{
	ui_set_link_transports(link_transports());
	if (atomic_set(&link_ups, 0) > 0) {
		ui_notify(UI_EVENT_APP_CONNECTED);
	}
	if (atomic_set(&link_downs, 0) > 0 && !companion_serial.isConnected()) {
		companion_session_cleanup();
		ui_notify(UI_EVENT_APP_DISCONNECTED);
	}
}

/* A binary companion client is listening on some transport (a USB text-CLI
 * session is not one). */
static bool companion_transport_up(void)
{
	return companion_serial.isConnected();
}

/* Inbound binary frames, on MESH_EVENT_BLE_RX. */
static void process_companion_rx(void)
{
	uint8_t buf[MAX_FRAME_SIZE];
	size_t len;
	bool handled = false;

	/* CompanionSerial reads nothing while it holds a reply */
	while ((len = companion_serial.checkRecvFrame(buf)) > 0) {
		handled = true;
#ifdef ZEPHCORE_LORA
		/* A contact dump survives commands in between (the app talks to us
		 * mid-sync); only CMD_APP_START or a disconnect ends it. */
		if (!companion_mesh_ptr->handleCmdFrame(buf, len)) {
			LOG_DBG("rx_process: unknown cmd 0x%02x len=%u", buf[0], (unsigned)len);
			uint8_t err_rsp[] = { 0x01, 0x01 };  /* PACKET_ERROR, ERR_UNSUPPORTED */
			companion_serial.writeFrame(err_rsp, sizeof(err_rsp));
		}
#endif
	}

	/* A command may have started a multi-frame response (the contact dump);
	 * pump it. Only after a handled frame: CONTACT_ITER also runs this
	 * function, and an unconditional post spun the main thread at 100% CPU. */
	if (handled) {
		k_event_post(&mesh_events, MESH_EVENT_CONTACT_ITER);
	}
}

/* The contact dump, as far as every connected transport has room
 * (MESH_EVENT_CONTACT_ITER); a transport's TX idle brings us back. Only while a
 * client is connected: with none no write succeeds and the loop would not end. */
static void run_contact_iteration(void)
{
#ifdef ZEPHCORE_LORA
	if (!companion_mesh_ptr) {
		return;
	}
	while (companion_serial.isConnected() && !companion_serial.isWriteBusy()) {
		if (!companion_mesh_ptr->continueContactIteration()) {
			break;  /* iteration complete */
		}
	}
#endif
}

/* The main thread: blocks until an event, plus the housekeeping tick. */
static void mesh_event_loop(void)
{
	LOG_INF("starting event-driven loop");
#ifdef ZEPHCORE_LORA
	mesh_thread = k_current_get();
#endif

	k_timer_start(&housekeeping_timer, K_MSEC(HOUSEKEEPING_INTERVAL_MS),
		      K_MSEC(HOUSEKEEPING_INTERVAL_MS));

	for (;;) {
		uint32_t events = k_event_wait(&mesh_events, MESH_EVENT_ALL, false, K_FOREVER);
		k_event_clear(&mesh_events, events);
		loopWakeNote(events);

		/* TX-idle resumes both the retained reply and commands left in RX.
		 * Housekeeping is a fallback if a transport loses its drain kick. */
		companion_serial.retry();

		if (events & MESH_EVENT_LINK) {
			process_link_change();
		}

#ifdef ZEPHCORE_LORA
		/* A reboot/power-off on another thread is waiting for this. */
		if ((events & MESH_EVENT_PERSIST) && companion_mesh_ptr) {
			persist_pending_writes();
			k_sem_give(&persist_done);
		}
		if (events & MESH_EVENT_MANUAL_SHUTDOWN) {
			bool notify = companion_serial.isConnected() &&
				companion_mesh_ptr->isVContactEnabled();
			if (notify) {
				companion_mesh_ptr->vcontactNotify("Выключение: кнопка");
				k_work_schedule(&manual_shutdown_work, K_MSEC(8000));
			} else {
				k_work_schedule(&manual_shutdown_work, K_NO_WAIT);
			}
		}
#endif

#ifdef ZEPHCORE_LORA
		/* UI button actions (advert, pref saves) */
		if (events & MESH_EVENT_UI_ACTION) {
			mesh_handle_ui_actions();
		}

		/* GPS wake/timeout. GNSS configuration blocks on a semaphore the
		 * system work queue signals, so it would deadlock there. */
		if (events & MESH_EVENT_GPS_ACTION) {
			gps_process_event();
			ui_set_gps_available(gps_is_available());  /* the boot probe may clear it */
		}
		if (events & MESH_EVENT_FALL_DETECTED) {
#if defined(ZEPHCORE_FALL_DETECTOR)
			if (fall_detector_is_enabled()) companion_fall_detected();
#endif
		}
		if (events & MESH_EVENT_FALL_PREALERT) {
#if defined(ZEPHCORE_FALL_DETECTOR)
			companion_fall_prealert();
#endif
		}
		if (events & MESH_EVENT_GPS_FIX) {
			companion_sos_process();
			companion_tracking_odometer_fix();
			companion_home_save_process();
			companion_nav_fix_process();
			companion_geo_fix_process();
		}
		if (events & MESH_EVENT_LORA_TX_DONE) companion_sos_tx_done();

		/* Inbound frames and CLI lines, before loop() drains what they
		 * queued. */
		if (events & (MESH_EVENT_BLE_RX | MESH_EVENT_CONTACT_ITER |
			      MESH_EVENT_HOUSEKEEPING)) {
			process_companion_rx();
#if ZEPHCORE_USB_STACK
			struct companion_cli_line c;
			while (k_msgq_get(&companion_cli_queue, &c, K_NO_WAIT) == 0) {
				companion_cli_run(c.buf);
			}
#endif
		}

		if (events & MESH_EVENT_CONTACT_ITER) {
			run_contact_iteration();
		}

		/* Radio/transport/TX events, and housekeeping for loop()'s own
		 * deadlines (lazy contact/channel flush, send timeout, keep-alive),
		 * which would otherwise wait for unrelated traffic. */
		if (companion_mesh_ptr &&
		    (events & (MESH_EVENT_LORA_RX | MESH_EVENT_LORA_TX_DONE |
			       MESH_EVENT_BLE_RX | MESH_EVENT_TX_DRAIN |
			       MESH_EVENT_HOUSEKEEPING))) {
			companion_mesh_ptr->loop();
		}

		if (events & MESH_EVENT_HOUSEKEEPING) {
			/* Noise floor, adaptive-CAD probe, RX watchdog: never on
			 * packet-driven events. */
			if (companion_mesh_ptr) {
				companion_mesh_ptr->maintenanceLoop();
			}

			/* Contact-dump watchdog: only the tx-idle callback pumps the
			 * dump, so one lost kick strands it. Re-kick only if the
			 * cursor did not move for a whole tick. */
			static int wd_last_iter_idx = -1;
			if (companion_mesh_ptr && companion_mesh_ptr->isContactIterActive()) {
				int idx = companion_mesh_ptr->getContactIterIdx();
				if (idx == wd_last_iter_idx) {
					LOG_WRN("contact dump watchdog: no progress at %d, resuming", idx);
					k_event_post(&mesh_events, MESH_EVENT_CONTACT_ITER);
				}
				wd_last_iter_idx = idx;
			} else {
				wd_last_iter_idx = -1;
			}

			/* Offline-queue watchdog (see msgWaitingWatchdog), only with
			 * a live client: a reconnect drains the queue anyway, and a
			 * prompt into a dead link would only delay the next one. */
			if (companion_mesh_ptr && companion_transport_up()) {
				companion_mesh_ptr->msgWaitingWatchdog();
			}

#if IS_ENABLED(CONFIG_BT)
			/* Advertising watchdog: a transient bt_le_adv_start failure
			 * otherwise leaves the node undiscoverable until reboot. */
			if (zephcore_ble_is_enabled() && !zephcore_ble_is_connected() && !zephcore_ble_is_advertising()) {
				LOG_WRN("BLE adv watchdog: not advertising, re-enabling");
				zephcore_ble_set_enabled(true);
			}
#endif

			mesh_housekeeping_ui_refresh();
#if IS_ENABLED(CONFIG_ZEPHCORE_COMPANION_WIFI) && defined(ZEPHCORE_LORA)
			companion_wifi_ui_refresh();
#endif

			if (companion_mesh_ptr) {
				companion_mesh_ptr->timeSyncTick();
			}

			/* Low-battery auto-shutdown and the v-contact alert, UI or
			 * not (app/PowerPolicy.cpp). */
			power_policy_tick();
		}

		/* Prefs changed off-main: the flash write happens here, and posts
		 * coalesce into one write. */
		if ((events & MESH_EVENT_PREFS_DIRTY) && companion_mesh_ptr) {
			save_prefs_to_flash();
		}

		companion_sos_process();
		companion_tracking_process();
		companion_home_save_process();
		companion_nav_link_check();
		uint32_t next_custom = companion_custom_ms_until_next();
		if (next_custom == UINT32_MAX) {
			k_timer_stop(&custom_timer);
		} else {
			k_timer_start(&custom_timer, K_MSEC(next_custom ? next_custom : 1), K_NO_WAIT);
		}
#endif

#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DESIGN_JOYSTICK)
		if (events & MESH_EVENT_JOYSTICK_LOOP) {
			joystick_ui_task.loop();
		}
#endif
	}
}

static void housekeeping_timer_fn(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	k_event_post(&mesh_events, MESH_EVENT_HOUSEKEEPING);
}

static void custom_timer_fn(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	k_event_post(&mesh_events, MESH_EVENT_CUSTOM_TIMER);
}

#ifdef ZEPHCORE_LORA
/* ISR context */
static void lora_rx_callback(void *user_data)
{
	ARG_UNUSED(user_data);
	k_event_post(&mesh_events, MESH_EVENT_LORA_RX);
}

/* Driver work queue, when an async TX finishes */
static void lora_tx_done_callback(void *user_data)
{
	ARG_UNUSED(user_data);
	k_event_post(&mesh_events, MESH_EVENT_LORA_TX_DONE);
}

/* The dispatcher queued a delayed packet: wake when the delay expires. */
static void tx_drain_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	k_event_post(&mesh_events, MESH_EVENT_TX_DRAIN);
}

static K_WORK_DELAYABLE_DEFINE(tx_drain_work, tx_drain_work_fn);

static void tx_queued_callback(uint32_t delay_ms, void *user_data)
{
	ARG_UNUSED(user_data);
	k_work_reschedule(&tx_drain_work, K_MSEC(delay_ms));
}
#endif

static mesh::ZephyrRTCClock rtc_clock;
static ZephyrDataStore data_store(rtc_clock);

#ifdef ZEPHCORE_LORA
static mesh::ZephyrBoard zephyr_board;

/* main() binds the radio to companion_mesh.prefs (setPrefs) before begin(). */
static const struct device *const lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
static mesh::LoRaRadio lora_radio(lora_dev, zephyr_board);

static uint16_t get_battery_mv(void)
{
	return zephyr_board.getBattMilliVolts();
}

static mesh::ZephyrMillisecondClock ms_clock;
static mesh::ZephyrRNG zephyr_rng;
static SimpleMeshTables mesh_tables;
static StaticPoolPacketManager packet_mgr;
/* A WiFi companion on a PSRAM board keeps this object (its contacts table and
 * offline queue are ~110 KB) in PSRAM, so WiFi and BLE fit in internal DRAM.
 * Safe: only the main thread touches it, never an ISR or a flash operation,
 * and the SoC boot zeroes .ext_ram.bss before any constructor runs. */
#if IS_ENABLED(CONFIG_ZEPHCORE_COMPANION_WIFI) && IS_ENABLED(CONFIG_ESP_SPIRAM)
#define COMPANION_MESH_SECTION __attribute__((section(".ext_ram.bss.companion_mesh")))
#else
#define COMPANION_MESH_SECTION
#endif
static CompanionMesh companion_mesh COMPANION_MESH_SECTION (lora_radio, ms_clock, zephyr_rng,
	rtc_clock, packet_mgr, mesh_tables, data_store);

static void save_prefs_to_flash(void)
{
	data_store.savePrefs(companion_mesh_ptr->prefs);
}

static bool companion_get_public_channel(const char *channel_name,
					 ChannelDetails &public_channel)
{
	if (!companion_mesh_ptr) {
		return false;
	}

	for (int i = 0; i < companion_mesh_ptr->getNumChannels(); i++) {
		ChannelDetails channel;
		if (companion_mesh_ptr->getChannel(i, channel) &&
		    strcmp(channel.name, channel_name) == 0) {
			public_channel = channel;
			return true;
		}
	}

	/* Public #channels use SHA-256(channel name)[0..15] as their PSK. */
	uint8_t psk[16];
	mesh::Utils::sha256(psk, sizeof(psk),
		(const uint8_t *)channel_name, (int)strlen(channel_name));
	ChannelDetails *created = companion_mesh_ptr->addChannel(
		channel_name, psk, sizeof(psk));
	if (!created) {
		LOG_WRN("unable to create %s channel", channel_name);
		return false;
	}

	public_channel = *created;
	/* SOS or shutdown can follow immediately, so retain the channel now. */
	data_store.saveChannels(companion_mesh_ptr);
	LOG_INF("created public channel %s", channel_name);
	return true;
}

static bool companion_get_emergency_channel(ChannelDetails &emergency_channel)
{
	return companion_get_public_channel("#zephcore", emergency_channel);
}

static void format_uptime(uint64_t uptime_ms, char *out, size_t out_len)
{
	/* Clamped at 99h59m so the shutdown notice always fits the mesh frame. */
	uint64_t min = (uptime_ms + 30000U) / 60000U;

	if (min > 99U * 60U + 59U) {
		min = 99U * 60U + 59U;
	}
	snprintf(out, out_len, "%uч%02uм", (unsigned)(min / 60U), (unsigned)(min % 60U));
}

/* Queue the low-battery notice in the #zephcore group. This is
 * deliberately called only by companion_shutdown_hook(), which is invoked by
 * ui_auto_shutdown_check() after its low-voltage confirmation — never by a
 * manual shutdown action. */
static bool companion_send_auto_shutdown_emergency(uint16_t battery_mv,
								uint32_t uptime_ms)
{
	ChannelDetails emergency_channel;
	if (!companion_get_emergency_channel(emergency_channel)) {
		return false;
	}

	char uptime[10];
	char text[160];
	char temperature[10] = "n/a";
	char gps[72];
	bool has_gps = gps_is_available();
	struct env_data env = {};
	if (env_sensors_read(&env) == 0) {
		if (env.has_temperature) {
				snprintf(temperature, sizeof(temperature), "%.1f°C",
					 (double)env.temperature_c);
		}
	}
	if (!env.has_temperature) {
		float mcu_temp = zephyr_board.getMCUTemperature();
		if (isfinite(mcu_temp)) {
				snprintf(temperature, sizeof(temperature), "%.1f°C", (double)mcu_temp);
		}
	}
	if (has_gps) {
		strcpy(gps, "🛰️ выключен");
	}
	if (has_gps && gps_is_enabled()) {
		struct gps_state_info gsi;
		struct gps_position pos = {};
		gps_get_state_info(&gsi);
		/* Prefer the GSV count (all satellites visible to the receiver); fall
		 * back to the GGA count used by the last/current navigation solution. */
		uint16_t sats_in_view = gsi.visible_satellites ?
			gsi.visible_satellites : gsi.satellites;
		/* Display clamp: keeps the notice inside the mesh text frame. */
		if (sats_in_view > 99) {
			sats_in_view = 99;
		}
		bool has_fix = gps_get_last_known_position(&pos);
		bool has_coordinates = pos.latitude_ndeg != 0 && pos.longitude_ndeg != 0;

		if (has_fix && has_coordinates) {
			char url[48];

			tracking_maps_url(url, sizeof(url), pos.latitude_ndeg / 1e9,
					  pos.longitude_ndeg / 1e9);
			snprintf(gps, sizeof(gps), "🛰️ %u fix\n📍 %s",
				 sats_in_view, url);
		} else if (has_fix) {
			snprintf(gps, sizeof(gps), "🛰️ %u fix", sats_in_view);
		} else {
			snprintf(gps, sizeof(gps), "🛰️ %u no fix", sats_in_view);
		}
	}
	format_uptime(uptime_ms, uptime, sizeof(uptime));
	if (has_gps) {
		snprintf(text, sizeof(text),
			 "Отключение!\n🪫 %u.%02uV\n🌡️ %s\n⏱️ %s\n%s",
			 battery_mv / 1000U, (battery_mv % 1000U) / 10U,
			 temperature, uptime, gps);
	} else {
		snprintf(text, sizeof(text),
			 "Отключение!\n🪫 %u.%02uV\n🌡️ %s\n⏱️ %s",
			 battery_mv / 1000U, (battery_mv % 1000U) / 10U,
			 temperature, uptime);
	}

	if (!companion_mesh_ptr->sendGroupMessage(rtc_clock.getCurrentTimeUnique(),
							  emergency_channel.channel,
							  companion_mesh_ptr->prefs.node_name,
							  text, (int)strlen(text))) {
		LOG_WRN("auto-shutdown: unable to queue #zephcore emergency message");
		return false;
	}

	LOG_INF("auto-shutdown: queued #zephcore emergency message: %s", text);
	return true;
}

#define SOS_FIX_TIMEOUT_MS (5U * 60U * 1000U)
#define SOS_FRESH_FIX_MAX_AGE_S 30U
#define SOS_WAITING_REPEAT_MS 30000U
#define FALL_ALARM_REPEAT_MS 30000U

enum companion_emergency_type {
	COMPANION_EMERGENCY_SOS,
	COMPANION_EMERGENCY_FALL,
};

struct companion_sos_state {
	bool pending;
	bool silent;
	enum companion_emergency_type type;
	uint32_t started_ms;
	uint32_t next_waiting_message_ms;
};

static struct companion_sos_state companion_sos;
static atomic_t companion_manual_sos_pending;
static struct {
	bool pending;
	bool fall;
	bool silent;
	uint32_t after_packets_sent;
} companion_sos_tone;
static struct {
	bool active;
	uint32_t next_alarm_ms;
} companion_fall_alarm_state;

static void companion_sos_ui_waiting(void)
{
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DISPLAY) && IS_ENABLED(CONFIG_ZEPHCORE_UI_DESIGN_BUTTON)
	if (companion_sos.type == COMPANION_EMERGENCY_SOS) {
		ui_pages_sos_waiting();
	} else {
		ui_pages_sos_clear();
	}
#endif
	ui_request_render();
}

static void companion_sos_ui_sent(bool success)
{
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DISPLAY) && IS_ENABLED(CONFIG_ZEPHCORE_UI_DESIGN_BUTTON)
	if (companion_sos.type == COMPANION_EMERGENCY_SOS) {
		ui_pages_sos_sent(success);
	} else {
		ui_pages_sos_clear();
	}
#else
	ARG_UNUSED(success);
#endif
	ui_request_render();
}

static void companion_sos_ui_clear(void)
{
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DISPLAY) && IS_ENABLED(CONFIG_ZEPHCORE_UI_DESIGN_BUTTON)
	ui_pages_sos_clear();
#endif
	ui_request_render();
}

static void companion_fall_alarm_play(void)
{
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
	/* Fall is a safety alarm: user mute applies to UI sounds, not this alert. */
	buzzer_play_force(MELODY_SOS);
#endif
}

static void companion_fall_alarm_start(void)
{
	companion_fall_alarm_state.active = true;
	companion_fall_alarm_play();
	companion_fall_alarm_state.next_alarm_ms = k_uptime_get_32() +
		FALL_ALARM_REPEAT_MS;
}

static void companion_fall_alarm_stop(void)
{
	companion_fall_alarm_state.active = false;
	companion_sos_tone.pending = false;
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
	buzzer_stop();
#endif
}

static void companion_fall_cancel_pending(void)
{
	if (!companion_sos.pending ||
	    companion_sos.type != COMPANION_EMERGENCY_FALL) {
		return;
	}

	memset(&companion_sos, 0, sizeof(companion_sos));
	atomic_set(&companion_manual_sos_pending, 0);
	companion_gps_user_left();
	companion_sos_ui_clear();
}

static bool companion_send_visible_group_message(ChannelDetails &channel,
						const char *text)
{
	uint32_t timestamp = rtc_clock.getCurrentTimeUnique();
	if (!companion_mesh_ptr->sendGroupMessage(timestamp, channel.channel,
			companion_mesh_ptr->prefs.node_name, text, (int)strlen(text))) {
		return false;
	}
	companion_mesh_ptr->queueLocalAutomatedChannelMessage(channel.channel,
			timestamp, text);
	return true;
}

/* Geofence alerts (ADR 0011): out over LoRa only, never queued into the
 * local group feed. */
static bool companion_send_hidden_group_message(ChannelDetails &channel,
					       const char *text)
{
	uint32_t timestamp = rtc_clock.getCurrentTimeUnique();
	return companion_mesh_ptr->sendGroupMessage(timestamp, channel.channel,
			companion_mesh_ptr->prefs.node_name, text, (int)strlen(text));
}

static void companion_send_fall_canceled_message(void)
{
	ChannelDetails sos_channel;
	static const char text[] = "❌ Падение отменено";

	if (!companion_get_public_channel("#sos", sos_channel)) {
		return;
	}
	if (!companion_send_visible_group_message(sos_channel, text)) {
		LOG_WRN("fall: unable to queue cancel message");
		return;
	}
	LOG_INF("fall: queued #sos cancel message");
}

static void companion_send_sos_canceled_message(void)
{
	ChannelDetails sos_channel;

	if (!companion_get_public_channel("#sos", sos_channel)) {
		return;
	}
	if (!companion_send_visible_group_message(sos_channel,
			COMPANION_SOS_CANCELED_TEXT)) {
		LOG_WRN("SOS: unable to queue cancel message");
		return;
	}
	LOG_INF("SOS: queued #sos cancel message");
}

extern "C" bool companion_sos_cancel_available(void)
{
	return atomic_get(&companion_manual_sos_pending) != 0;
}

extern "C" void companion_sos_cancel_from_ui(void)
{
	if (!companion_sos_button_cancelable(companion_sos.pending,
			companion_sos.type == COMPANION_EMERGENCY_FALL)) {
		return;
	}

	memset(&companion_sos, 0, sizeof(companion_sos));
	atomic_set(&companion_manual_sos_pending, 0);
	companion_gps_user_left();
	companion_sos_tone.pending = false;
	companion_sos_ui_clear();
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
	buzzer_play_force(MELODY_FALL_CANCELED);
#endif
	companion_send_sos_canceled_message();
	LOG_INF("SOS canceled by button");
}

extern "C" void companion_fall_alarm_acknowledge_from_ui(void)
{
	if (!companion_fall_alarm_state.active) {
		return;
	}
	companion_fall_alarm_stop();
	companion_fall_cancel_pending();
#if defined(ZEPHCORE_FALL_DETECTOR)
	fall_detector_reset();
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
	/* Fall acknowledgement must be audible even when ordinary UI sounds mute. */
	buzzer_play_force(MELODY_FALL_CANCELED);
#endif
	companion_send_fall_canceled_message();
	LOG_INF("fall alarm acknowledged by button");
}

static bool companion_sos_has_fresh_fix(void)
{
	struct gps_position pos = {};
	struct gps_state_info gsi;

	if (!gps_get_last_known_position(&pos) ||
	    pos.latitude_ndeg == 0 || pos.longitude_ndeg == 0) {
		return false;
	}

	gps_get_state_info(&gsi);
	return gsi.last_fix_age_s <= SOS_FRESH_FIX_MAX_AGE_S;
}

static bool companion_send_sos_waiting_message(void)
{
	ChannelDetails sos_channel;
	const char *text = companion_sos.type == COMPANION_EMERGENCY_FALL ?
		"🆘 Падение!\nОжидание GPS-фикса..." : "🆘 SOS!\nОжидание GPS-фикса...";

	if (!companion_get_public_channel("#sos", sos_channel)) {
		return false;
	}

	if (!companion_send_visible_group_message(sos_channel, text)) {
		LOG_WRN("emergency: unable to queue waiting message");
		return false;
	}

	LOG_INF("emergency: queued #sos waiting message");
	return true;
}

static bool companion_send_sos_message(bool allow_coordinates)
{
	ChannelDetails sos_channel;
	if (!companion_get_public_channel("#sos", sos_channel)) {
		return false;
	}

	char temperature[16] = "n/a";
	char gps[72];
	char text[192];
	struct env_data env = {};
	bool has_gps = gps_is_available();
	const char *prefix = companion_sos.type == COMPANION_EMERGENCY_FALL ?
		"🆘 Падение!\nНужна помощь!" : "🆘 SOS!";

	if (env_sensors_read(&env) == 0) {
		if (env.has_temperature) {
				snprintf(temperature, sizeof(temperature), "%.1f°C",
					 (double)env.temperature_c);
		}
	}
	if (!env.has_temperature) {
		float mcu_temp = zephyr_board.getMCUTemperature();
		if (isfinite(mcu_temp)) {
				snprintf(temperature, sizeof(temperature), "%.1f°C", (double)mcu_temp);
		}
	}

	if (has_gps) {
		struct gps_state_info gsi;
		struct gps_position pos = {};
		gps_get_state_info(&gsi);
		uint16_t sats_in_view = gsi.visible_satellites ?
			gsi.visible_satellites : gsi.satellites;
		/* Display clamp: keeps the report inside the mesh text frame. */
		if (sats_in_view > 99) {
			sats_in_view = 99;
		}
		bool has_fix = gps_get_last_known_position(&pos);
		bool has_coordinates = pos.latitude_ndeg != 0 && pos.longitude_ndeg != 0;

		if (has_fix && has_coordinates && allow_coordinates) {
			char url[48];

			tracking_maps_url(url, sizeof(url), pos.latitude_ndeg / 1e9,
					  pos.longitude_ndeg / 1e9);
			snprintf(gps, sizeof(gps), "🛰️ %u fix\n📍 %s",
				 sats_in_view, url);
		} else if (has_fix && allow_coordinates) {
			snprintf(gps, sizeof(gps), "🛰️ %u fix", sats_in_view);
		} else {
			snprintf(gps, sizeof(gps), "🛰️ %u no fix", sats_in_view);
		}
	}

	uint16_t battery_mv = get_battery_mv();
	size_t name_prefix = strlen(companion_mesh.prefs.node_name) + 2;
	size_t budget = ((size_t)MAX_TEXT_LEN > name_prefix ?
			 MAX_TEXT_LEN - name_prefix : 0) + 1;
	int n = -1;

	/* The temperature line is the first to go when the node name leaves too
	 * little of MAX_TEXT_LEN, then the battery line: the coordinates —
	 * always last — must reach the group whole. */
	if (has_gps) {
		n = snprintf(text, budget, "%s\n🔋 %u.%02uV\n🌡️ %s\n%s",
			     prefix, battery_mv / 1000U,
			     (battery_mv % 1000U) / 10U, temperature, gps);
		if (n < 0 || (size_t)n >= budget) {
			n = snprintf(text, budget, "%s\n🔋 %u.%02uV\n%s",
				     prefix, battery_mv / 1000U,
				     (battery_mv % 1000U) / 10U, gps);
		}
		if (n < 0 || (size_t)n >= budget) {
			snprintf(text, budget, "%s\n%s", prefix, gps);
		}
	} else {
		snprintf(text, sizeof(text), "%s\n🔋 %u.%02uV\n🌡️ %s", prefix,
			 battery_mv / 1000U, (battery_mv % 1000U) / 10U,
			 temperature);
	}

	uint32_t packets_before = lora_radio.getPacketsSent();
	bool tx_was_active = lora_radio.isTxActive();
	if (!companion_send_visible_group_message(sos_channel, text)) {
		LOG_WRN("SOS: unable to queue #sos message");
		return false;
	}

	LOG_INF("SOS: queued #sos message: %s", text);
	/* Waiting messages are silent. This marker is set only for the final SOS,
	 * so its alarm plays once after that packet has actually left the radio. */
	companion_sos_tone.after_packets_sent = packets_before +
		(tx_was_active ? 2U : 1U);
	companion_sos_tone.fall = companion_sos.type == COMPANION_EMERGENCY_FALL;
	companion_sos_tone.silent = companion_sos.silent;
	companion_sos_tone.pending = true;
	return true;
}

static bool companion_sos_send_now(bool allow_coordinates)
{
	bool success = companion_send_sos_message(allow_coordinates);
	companion_sos_ui_sent(success);
	return success;
}

static bool companion_sos_finish(bool fresh_fix)
{
	bool success = companion_sos_send_now(fresh_fix);

	/* SOS temporarily forces continuous acquisition; the last GPS user out
	 * powers the GPS down (ADR 0011). */
	memset(&companion_sos, 0, sizeof(companion_sos));
	atomic_set(&companion_manual_sos_pending, 0);
	companion_gps_user_left();
	return success;
}

static bool companion_emergency_request(enum companion_emergency_type type,
						char *reply, bool play_confirm, bool silent)
{
	/* A deliberate SOS takes over completely from a fall alarm without
	 * emitting a cancel notice; it is a new emergency, not an acknowledgement. */
	if (type == COMPANION_EMERGENCY_SOS && companion_fall_alarm_state.active) {
		companion_fall_alarm_stop();
	}

	if (companion_sos.pending) {
		/* A new emergency replaces the active one while retaining the original
		 * GPS state, so only an explicit Fall cancellation restores it. */
		companion_sos.type = type;
		companion_sos.silent = silent;
		atomic_set(&companion_manual_sos_pending,
			companion_sos_button_cancelable(true,
				type == COMPANION_EMERGENCY_FALL));
		companion_sos.started_ms = k_uptime_get_32();
		companion_sos_ui_waiting();
		companion_send_sos_waiting_message();
		companion_sos.next_waiting_message_ms = companion_sos.started_ms +
			SOS_WAITING_REPEAT_MS;
		if (type == COMPANION_EMERGENCY_FALL) {
			companion_fall_alarm_start();
		}
		gps_set_poll_interval_sec(0);
		if (gps_is_enabled()) {
			gps_request_fresh_fix();
		} else {
			gps_enable(true);
		}
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
		if (play_confirm) {
			buzzer_play(MELODY_SOS_CONFIRM);
		}
#endif
		strcpy(reply, type == COMPANION_EMERGENCY_FALL ?
			"Fall: GPS search restarted (max 5 min)" :
			"SOS: GPS search restarted (max 5 min)");
		return true;
	}

	if (play_confirm) {
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
	/* SOS accepted: a short rising acknowledgement. The TX-complete alert
	 * below remains the Morse SOS melody. */
	buzzer_play(MELODY_SOS_CONFIRM);
#endif
	}

	/* No GPS hardware: send the final message immediately. */
	companion_sos.type = type;
	companion_sos.silent = silent;
	if (type == COMPANION_EMERGENCY_FALL) {
		companion_fall_alarm_start();
	}
	if (!gps_is_available()) {
		strcpy(reply, companion_sos_send_now(false) ?
		       "OK - emergency sent" : "ERROR: emergency send failed");
		return true;
	}

	/* A recent validated coordinate is safe to send right now. */
	bool gps_was_enabled = gps_is_enabled();
	if (gps_was_enabled && companion_sos_has_fresh_fix()) {
		strcpy(reply, companion_sos_send_now(true) ?
		       "OK - emergency sent with GPS" : "ERROR: emergency send failed");
		return true;
	}

	/* GPS hardware is present but its coordinate is stale, absent, or GPS is
	 * off. Announce the SOS immediately, then hold the detailed message for a
	 * fresh fix or timeout. */
	companion_sos.pending = true;
	companion_sos.type = type;
	companion_sos.silent = silent;
	atomic_set(&companion_manual_sos_pending,
		companion_sos_button_cancelable(true,
			type == COMPANION_EMERGENCY_FALL));
	companion_sos.started_ms = k_uptime_get_32();
	companion_sos_ui_waiting();
	companion_send_sos_waiting_message();
	companion_sos.next_waiting_message_ms = companion_sos.started_ms +
		SOS_WAITING_REPEAT_MS;
	/* Hold GPS awake until a fresh fix or the SOS timeout. */
	gps_set_poll_interval_sec(0);
	if (gps_was_enabled) {
		gps_request_fresh_fix();
	} else {
		gps_enable(true);
	}

	strcpy(reply, type == COMPANION_EMERGENCY_FALL ?
		"Fall: waiting for GPS fix (max 5 min)" :
		"SOS: waiting for GPS fix (max 5 min)");
	return true;
}

static bool companion_sos_request(char *reply, bool silent = false)
{
	return companion_emergency_request(COMPANION_EMERGENCY_SOS, reply, !silent, silent);
}

static void companion_fall_detected(void)
{
	char reply[CLI_REPLY_SIZE];
	if (companion_emergency_request(COMPANION_EMERGENCY_FALL, reply, false, false)) {
		LOG_WRN("%s", reply);
	}
}

static void companion_fall_prealert(void)
{
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
	buzzer_play_force(MELODY_FALL_CANCELED);
#endif
}

extern "C" void companion_sos_request_from_ui(void)
{
	char reply[CLI_REPLY_SIZE];
	/* UI and CLI share the same acknowledgement and SOS state machine. */
	companion_sos_request(reply);
}

static void companion_sos_process(void)
{
	uint32_t now_ms = k_uptime_get_32();
	if (companion_fall_alarm_state.active &&
	    (int32_t)(now_ms - companion_fall_alarm_state.next_alarm_ms) >= 0) {
		companion_fall_alarm_play();
		companion_fall_alarm_state.next_alarm_ms = now_ms + FALL_ALARM_REPEAT_MS;
	}

	if (!companion_sos.pending) {
		return;
	}

	struct gps_position pos = {};
	bool fresh_fix = companion_sos_has_fresh_fix() &&
		gps_get_last_known_position(&pos) &&
		pos.latitude_ndeg != 0 && pos.longitude_ndeg != 0 &&
		pos.timestamp_ms > companion_sos.started_ms;
	uint32_t elapsed_ms = k_uptime_get_32() - companion_sos.started_ms;
	if (fresh_fix || elapsed_ms >= SOS_FIX_TIMEOUT_MS) {
		companion_sos_finish(fresh_fix);
		return;
	}

	if ((int32_t)(now_ms - companion_sos.next_waiting_message_ms) >= 0) {
		companion_send_sos_waiting_message();
		companion_sos.next_waiting_message_ms = now_ms + SOS_WAITING_REPEAT_MS;
	}
}

static void companion_sos_tx_done(void)
{
	uint32_t packets_sent = lora_radio.getPacketsSent();
	if (companion_sos_tone.pending &&
	    packets_sent >= companion_sos_tone.after_packets_sent) {
		bool play = !companion_sos_tone.silent &&
			(!companion_sos_tone.fall || companion_fall_alarm_state.active);
		companion_sos_tone.pending = false;
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
		if (play) {
			/* Fall's repeating alarm is forced separately. The final transmission
			 * respects the user's buzzer preference for both SOS and Fall. */
			buzzer_play(MELODY_SOS);
		}
#else
		ARG_UNUSED(play);
#endif
	}
}

#define TRACKING_MIN_INTERVAL_MIN 5U
#define TRACKING_MOVEMENT_METERS 50.0

struct companion_tracking_state {
	bool enabled;
	bool silent_reports;
	uint32_t started_ms;
	int64_t next_report_ms;
	bool has_last_sent_position;
	int64_t last_sent_lat_ndeg;
	int64_t last_sent_lon_ndeg;
};

static struct companion_tracking_state companion_tracking;
static struct tracking_odometer tracking_odo;

/* ===== GPS HOME: save the point and navigate to it ===== */
#define HOME_FIX_TIMEOUT_MS (5U * 60U * 1000U)
#define HOME_ARRIVAL_M 15.0
#define NAV_LINK_CHECK_MS 30000U
#define HOME_NAV_REFRESH_MS 5000U

struct companion_home_state {
	bool save_active;
	bool nav_active;
	bool gps_was_enabled;
	bool travel_has_last;
	bool link_lost_sent;
	uint8_t selected; /* 0..9; always 0 after boot */
	uint32_t saved_duty_sec;
	uint32_t save_started_ms;
	struct nav_wrongway ww;
	uint32_t ladder_ms;    /* last wrong-way sample (0 = prime on next fix) */
	uint32_t report_ms;    /* next vContact nav report (0 = on next fix) */
	uint32_t telemetry_ms; /* next LoRa nav telemetry (0 = on next fix) */
	uint32_t nav_started_ms; /* travel time origin for the arrival report */
	float report_last_dist_m; /* distance at the last LoRa report (<0 = prime) */
	double travel_m;
	int64_t travel_last_lat_ndeg;
	int64_t travel_last_lon_ndeg;
};
static struct companion_home_state companion_home;

/* ===== Geofence: own position vs one saved point (ADR 0011) ===== */
#define GEO_FLAG_ENABLED	BIT(0)
#define GEO_FLAG_SAVE_IS_GEO	BIT(1) /* the active home save captures the geo point */

struct companion_geo_state {
	uint8_t flags;
	struct geofence_state fsm;
};
static struct companion_geo_state companion_geo;

/* A GPS user (tracking, nav, geo, sos) has just left. While one of the
 * higher-rank users remains, the GPS is left alone; while only geo remains,
 * its 500 s pin is re-asserted; with nobody left the GPS powers down and the
 * configured duty (prefs.gps_interval — never written by features) returns. */
static void companion_gps_user_left(void)
{
	if (companion_tracking.enabled || companion_home.nav_active ||
	    companion_sos.pending) {
		return;
	}
	if (companion_geo.flags & GEO_FLAG_ENABLED) {
		if (!gps_is_enabled()) {
			gps_enable(true);
		}
		gps_set_poll_interval_sec(GEO_DUTY_SEC);
		return;
	}
	gps_set_poll_interval_sec(companion_mesh.prefs.gps_interval);
	gps_enable(false);
}

static uint32_t companion_custom_ms_until_next(void)
{
	const uint32_t now = k_uptime_get_32();
	uint32_t next = UINT32_MAX;
	auto include = [&](uint32_t deadline) {
		int32_t remaining = (int32_t)(deadline - now);
		uint32_t delay = remaining <= 0 ? 0 : (uint32_t)remaining;
		if (delay < next) next = delay;
	};
	if (companion_sos.pending) {
		include(companion_sos.started_ms + SOS_FIX_TIMEOUT_MS);
		include(companion_sos.next_waiting_message_ms);
	}
	if (companion_fall_alarm_state.active)
		include(companion_fall_alarm_state.next_alarm_ms);
	if (companion_tracking.enabled) {
		int64_t remaining = companion_tracking.next_report_ms - k_uptime_get();
		if (remaining <= 0) next = 0;
		else if ((uint64_t)remaining < next) next = (uint32_t)remaining;
	}
	if (companion_home.nav_active) {
		include(now + NAV_LINK_CHECK_MS);
	}
	if (companion_home.save_active) {
		include(companion_home.save_started_ms + HOME_FIX_TIMEOUT_MS);
	}
	return next;
}

static uint32_t companion_tracking_interval_ms(void)
{
	return (uint32_t)companion_mesh.prefs.tracking_interval_minutes * 60000U;
}

static void companion_tracking_ui_update(void)
{
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DISPLAY) && IS_ENABLED(CONFIG_ZEPHCORE_UI_DESIGN_BUTTON)
	ui_pages_set_tracking(companion_tracking.enabled,
		companion_mesh.prefs.tracking_interval_minutes);
#endif
	ui_request_render();
}

static bool companion_tracking_moved_min_distance(const struct gps_position &pos)
{
	if (!companion_tracking.has_last_sent_position) {
		return true;
	}

	const double deg_to_rad = 0.017453292519943295;
	double lat0 = companion_tracking.last_sent_lat_ndeg / 1e9;
	double lat1 = pos.latitude_ndeg / 1e9;
	double lat_delta = (lat1 - lat0) * deg_to_rad;
	double lon_delta = (pos.longitude_ndeg / 1e9 -
		companion_tracking.last_sent_lon_ndeg / 1e9) * deg_to_rad;
	double x = lon_delta * cos((lat0 + lat1) * 0.5 * deg_to_rad);
	double y = lat_delta;
	double distance_sq = (6371000.0 * x) * (6371000.0 * x) +
		(6371000.0 * y) * (6371000.0 * y);

	return distance_sq >= TRACKING_MOVEMENT_METERS * TRACKING_MOVEMENT_METERS;
}

static bool companion_send_tracking_message(const struct gps_position &pos)
{
	ChannelDetails tracks_channel;
	const char *group_name = companion_mesh.prefs.tracking_group_name;
	if (!companion_get_public_channel(group_name, tracks_channel)) {
		return false;
	}

	char text[160];
	tracking_format_report(text, sizeof(text),
			       pos.latitude_ndeg / 1e9, pos.longitude_ndeg / 1e9,
			       pos.speed_mms, pos.bearing_mdeg);
	if (!companion_send_visible_group_message(tracks_channel, text)) {
		LOG_WRN("tracking: unable to queue %s message", group_name);
		return false;
	}

	LOG_INF("tracking: queued %s message: %s", group_name, text);
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
	if (!companion_tracking.silent_reports) {
		buzzer_play(MELODY_NAV_ON);
	}
#endif
	return true;
}

static void companion_tracking_odometer_fix(void)
{
	if (!companion_tracking.enabled) {
		return;
	}
	struct gps_position pos;

	if (!gps_get_last_known_position(&pos)) {
		return;
	}
	tracking_odometer_on_fix(&tracking_odo, k_uptime_get(),
				 pos.latitude_ndeg, pos.longitude_ndeg, pos.speed_mms);
}

static void companion_send_tracking_summary(void)
{
	ChannelDetails tracks_channel;
	const char *group_name = companion_mesh.prefs.tracking_group_name;
	if (!companion_get_public_channel(group_name, tracks_channel)) {
		return;
	}

	struct gps_position pos;
	bool has_fix = gps_get_last_known_position(&pos);
	char text[160];
	tracking_format_summary(text, sizeof(text), &tracking_odo,
				has_fix ? pos.latitude_ndeg : 0,
				has_fix ? pos.longitude_ndeg : 0, has_fix);
	if (!companion_send_visible_group_message(tracks_channel, text)) {
		LOG_WRN("tracking: unable to queue %s summary", group_name);
		return;
	}
	LOG_INF("tracking: queued %s summary: %s", group_name, text);
}

static bool companion_tracking_set_enabled(bool enabled, char *reply, bool silent = false)
{
	if (enabled == companion_tracking.enabled) {
		snprintf(reply, CLI_REPLY_SIZE, "tracking: %s (%u min)",
			 enabled ? "on" : "off",
			 companion_mesh.prefs.tracking_interval_minutes);
		return true;
	}

	if (enabled) {
		if (!gps_is_available()) {
			strcpy(reply, "ERROR: GPS unavailable");
			return false;
		}
		if (companion_home.save_active &&
		    (companion_geo.flags & GEO_FLAG_SAVE_IS_GEO)) {
			strcpy(reply, "ERROR: saving geo point");
			return false;
		}
		bool gps_was_enabled = gps_is_enabled();
		companion_tracking.enabled = true;
		companion_tracking.silent_reports = silent;
		companion_tracking.started_ms = k_uptime_get_32();
		tracking_odometer_reset(&tracking_odo, k_uptime_get());
		companion_tracking.next_report_ms = k_uptime_get() +
			companion_tracking_interval_ms();
		gps_set_poll_interval_sec(0);
		if (!gps_was_enabled) {
			gps_enable(true);
		} else {
			gps_request_fresh_fix();
		}
		companion_tracking_ui_update();
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
		if (!silent) {
			buzzer_play(MELODY_TRACKING_ON);
		}
#endif
		snprintf(reply, CLI_REPLY_SIZE, "OK - tracking on (%u min)",
			 companion_mesh.prefs.tracking_interval_minutes);
		return true;
	}

	tracking_odometer_finish(&tracking_odo, k_uptime_get());
	companion_send_tracking_summary();
	memset(&companion_tracking, 0, sizeof(companion_tracking));
	companion_gps_user_left();
	companion_tracking_ui_update();
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
	if (!silent) {
		buzzer_play(MELODY_TRACKING_OFF);
	}
#endif
	strcpy(reply, "OK - tracking off");
	return true;
}

/* Geofence toggle (ADR 0011). Tracking, nav and sos outrank geo: while one
 * of them runs, geo leaves the GPS alone. Duty memory is prefs.gps_interval
 * and the last GPS user out powers the GPS down. */
static bool companion_geo_set_enabled(bool enabled, char *reply)
{
	if (companion_home.save_active) {
		strcpy(reply, "ERROR: saving point");
		return false;
	}
	bool on = (companion_geo.flags & GEO_FLAG_ENABLED) != 0;

	if (enabled == on) {
		snprintf(reply, CLI_REPLY_SIZE, "geo: %s (%u m)",
			 enabled ? "on" : "off",
			 companion_mesh.prefs.geo_radius_m);
		return true;
	}
	if (enabled) {
		if (!gps_is_available()) {
			strcpy(reply, "ERROR: GPS unavailable");
			return false;
		}
		companion_geo.flags |= GEO_FLAG_ENABLED;
		geofence_reset(&companion_geo.fsm);
		if (!companion_tracking.enabled && !companion_home.nav_active &&
		    !companion_sos.pending) {
			if (!gps_is_enabled()) {
				gps_enable(true);
			} else {
				gps_request_fresh_fix();
			}
			gps_set_poll_interval_sec(GEO_DUTY_SEC);
		}
		companion_mesh.prefs.geo_on = 1;
		save_prefs_to_flash();
		snprintf(reply, CLI_REPLY_SIZE, "OK - geo on (radius %u m)",
			 companion_mesh.prefs.geo_radius_m);
		return true;
	}
	companion_geo.flags &= (uint8_t)~GEO_FLAG_ENABLED;
	geofence_reset(&companion_geo.fsm);
	companion_mesh.prefs.geo_on = 0;
	save_prefs_to_flash();
	companion_gps_user_left();
	strcpy(reply, "OK - geo off");
	return true;
}

extern "C" void companion_tracking_toggle_from_ui(void)
{
	char reply[CLI_REPLY_SIZE];
	if (companion_tracking_set_enabled(!companion_tracking.enabled, reply)) {
		companion_mesh_ptr->vcontactNotify(companion_tracking.enabled ?
			"Tracking: вкл" : "Tracking: выкл");
	}
}

static void companion_home_restore_gps(void)
{
	gps_set_poll_interval_sec(companion_home.saved_duty_sec);
	if (!companion_home.gps_was_enabled) {
		gps_enable(false);
	}
}

static void companion_home_start_save(bool geo_point)
{
	if (companion_home.save_active) {
		return;
	}
	companion_home.gps_was_enabled = gps_is_enabled();
	companion_home.saved_duty_sec = gps_get_poll_interval_sec();
	companion_home.save_active = true;
	companion_home.save_started_ms = (uint32_t)k_uptime_get();
	if (geo_point) {
		companion_geo.flags |= GEO_FLAG_SAVE_IS_GEO;
	} else {
		companion_geo.flags &= (uint8_t)~GEO_FLAG_SAVE_IS_GEO;
	}
	gps_set_poll_interval_sec(0);
	if (!companion_home.gps_was_enabled) {
		gps_enable(true);
	}
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DISPLAY) && IS_ENABLED(CONFIG_ZEPHCORE_UI_DESIGN_BUTTON)
	if (!geo_point) {
		ui_pages_set_home_status(HOME_STATUS_WAITING);
	}
#endif
}

/* One save pipeline for wp save and geo save (ADR 0011): force a fix,
 * capture it, return the GPS to what it was. GEO_FLAG_SAVE_IS_GEO picks
 * the target. */
static void companion_home_save_process(void)
{
	if (!companion_home.save_active) {
		return;
	}
	bool geo_point = (companion_geo.flags & GEO_FLAG_SAVE_IS_GEO) != 0;
	struct gps_position pos = {};
	bool fresh_fix = gps_get_last_known_position(&pos) &&
		pos.latitude_ndeg != 0 && pos.longitude_ndeg != 0 &&
		pos.timestamp_ms > companion_home.save_started_ms &&
		(k_uptime_get() - pos.timestamp_ms) <= 30000;

	if (fresh_fix) {
		char msg[160], url[48];

		if (geo_point) {
			companion_mesh.prefs.geo_lat_e7 = (int32_t)(pos.latitude_ndeg / 100);
			companion_mesh.prefs.geo_lon_e7 = (int32_t)(pos.longitude_ndeg / 100);
			companion_mesh.prefs.geo_saved = 1;
			tracking_maps_url(url, sizeof(url),
					  companion_mesh.prefs.geo_lat_e7 / 1e7,
					  companion_mesh.prefs.geo_lon_e7 / 1e7);
			snprintf(msg, sizeof(msg), "💾 Центр геозоны сохранен!\n%s", url);
		} else {
			int i = companion_home.selected;

			companion_mesh.prefs.home_lat[i] = pos.latitude_ndeg / 1e9;
			companion_mesh.prefs.home_lon[i] = pos.longitude_ndeg / 1e9;
			companion_mesh.prefs.home_saved[i] = 1;
			if (i != 0 && strcmp(companion_mesh.prefs.home_name[i], "FREE") == 0) {
				strcpy(companion_mesh.prefs.home_name[i], "SAVED");
			}
			tracking_maps_url(url, sizeof(url),
					  companion_mesh.prefs.home_lat[i],
					  companion_mesh.prefs.home_lon[i]);
			snprintf(msg, sizeof(msg),
				 "💾 Точка успешно сохранена!\n📍 %d-%s\n%s",
				 i + 1, companion_mesh.prefs.home_name[i], url);
		}
		save_prefs_to_flash();
		companion_mesh.vcontactNotify(msg);
		companion_home.save_active = false;
		companion_geo.flags &= (uint8_t)~GEO_FLAG_SAVE_IS_GEO;
		companion_home_restore_gps();
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DISPLAY) && IS_ENABLED(CONFIG_ZEPHCORE_UI_DESIGN_BUTTON)
		if (!geo_point) {
			ui_pages_set_home_status(HOME_STATUS_SAVED);
		}
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
		buzzer_play(MELODY_HOME_SAVED);
#endif
		LOG_INF("%s saved: %.6f %.6f", geo_point ? "geo point" : "home point",
			geo_point ? companion_mesh.prefs.geo_lat_e7 / 1e7
				  : companion_mesh.prefs.home_lat[companion_home.selected],
			geo_point ? companion_mesh.prefs.geo_lon_e7 / 1e7
				  : companion_mesh.prefs.home_lon[companion_home.selected]);
		return;
	}
	if (k_uptime_get() - companion_home.save_started_ms >= HOME_FIX_TIMEOUT_MS) {
		companion_home.save_active = false;
		companion_geo.flags &= (uint8_t)~GEO_FLAG_SAVE_IS_GEO;
		companion_home_restore_gps();
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DISPLAY) && IS_ENABLED(CONFIG_ZEPHCORE_UI_DESIGN_BUTTON)
		if (!geo_point) {
			ui_pages_set_home_status(HOME_STATUS_NOFIX);
		}
#endif
		LOG_WRN("%s save: no GPS fix in time", geo_point ? "geo" : "home");
	}
}

static void companion_nav_set_enabled(bool enabled, char *reply)
{
	if (enabled == companion_home.nav_active) {
		snprintf(reply, CLI_REPLY_SIZE, "nav: %s", enabled ? "on" : "off");
		return;
	}
	if (enabled) {
		if (!gps_is_available()) {
			strcpy(reply, "ERROR: GPS unavailable");
			return;
		}
		companion_home.nav_active = true;
		nav_wrongway_reset(&companion_home.ww);
		companion_home.travel_m = 0;
		companion_home.travel_has_last = false;
		companion_home.ladder_ms = 0;
		companion_home.report_ms = 0;
		companion_home.telemetry_ms = 0;
		companion_home.report_last_dist_m = -1.0f;
		companion_home.link_lost_sent = false;
		companion_home.nav_started_ms = (uint32_t)k_uptime_get();
		gps_set_poll_interval_sec(0);
		if (!gps_is_enabled()) {
			gps_enable(true);
		}
		snprintf(reply, CLI_REPLY_SIZE, "OK - Navigation to %d-%s  %.5f,%.5f",
			 companion_home.selected + 1,
			 companion_mesh.prefs.home_name[companion_home.selected],
			 companion_mesh.prefs.home_lat[companion_home.selected],
			 companion_mesh.prefs.home_lon[companion_home.selected]);
		if (companion_mesh.prefs.nav_report_interval_min != 0) {
			char msg[192];
			struct gps_position pos = {};
			double home_lat =
				companion_mesh.prefs.home_lat[companion_home.selected];
			double home_lon =
				companion_mesh.prefs.home_lon[companion_home.selected];
			int n = snprintf(msg, sizeof(msg),
					 "🚀 Начало навигации к точке\n📍 %d-%s",
					 companion_home.selected + 1,
					 companion_mesh.prefs.home_name[companion_home.selected]);

			if (gps_is_enabled() && gps_get_last_known_position(&pos) &&
			    pos.valid &&
			    (k_uptime_get() - pos.timestamp_ms) <= 30000) {
				char rep[48];

				nav_report_str(rep, sizeof(rep),
					tracking_distance_m(pos.latitude_ndeg,
						pos.longitude_ndeg,
						(int64_t)(home_lat * 1e9),
						(int64_t)(home_lon * 1e9)),
					nav_bearing_deg(pos.latitude_ndeg / 1e9,
						pos.longitude_ndeg / 1e9,
						home_lat, home_lon));
				n += snprintf(msg + n, sizeof(msg) - n, "\n📏 %s", rep);
				/* The start notice doubles as the first report. */
				companion_home.report_ms = (uint32_t)k_uptime_get() +
					(uint32_t)companion_mesh.prefs.nav_report_interval_min * 60000U;
			}
			{
				char url[48];

				tracking_maps_url(url, sizeof(url), home_lat, home_lon);
				snprintf(msg + n, sizeof(msg) - n, "\n%s", url);
			}
			companion_mesh.vcontactNotify(msg);
		}
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
		buzzer_play(MELODY_NAV_ON);
#endif
	} else if (companion_home.save_active) {
		strcpy(reply, "ERROR: saving home point");
		return;
	} else {
		companion_home.nav_active = false;
		companion_gps_user_left();
		strcpy(reply, "OK - nav stop");
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
		buzzer_play(MELODY_NAV_OFF);
#endif
	}
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DISPLAY) && IS_ENABLED(CONFIG_ZEPHCORE_UI_DESIGN_BUTTON)
	ui_pages_set_nav_active(companion_home.nav_active);
#endif
}

extern "C" void companion_home_save_from_ui(void)
{
	companion_home_start_save(false);
}

extern "C" void companion_nav_toggle_from_ui(void)
{
	char reply[CLI_REPLY_SIZE];
	companion_nav_set_enabled(!companion_home.nav_active, reply);
}

extern "C" void companion_wp_cycle_from_ui(void)
{
	companion_home.selected = (companion_home.selected + 1) % 10;
	ui_request_render();
}

extern "C" bool companion_home_get_point(double *lat, double *lon, bool *saved,
					const char **name, int *index)
{
	int i = companion_home.selected;
	*lat = companion_mesh.prefs.home_lat[i];
	*lon = companion_mesh.prefs.home_lon[i];
	*saved = companion_mesh.prefs.home_saved[i] != 0;
	*name = companion_mesh.prefs.home_name[i];
	*index = i;
	return true;
}

extern "C" bool companion_nav_active(void)
{
	return companion_home.nav_active;
}

/* ZEPHCORE: navigation telemetry to the tracking group ("nav telemetry"). */
static bool companion_send_nav_telemetry_message(const char *text)
{
	ChannelDetails tracks_channel;
	const char *group_name = companion_mesh.prefs.tracking_group_name;

	if (!companion_get_public_channel(group_name, tracks_channel)) {
		return false;
	}
	if (!companion_send_visible_group_message(tracks_channel, text)) {
		LOG_WRN("nav telemetry: unable to queue %s message", group_name);
		return false;
	}
	LOG_INF("nav telemetry: queued %s message: %s", group_name, text);
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
	buzzer_play(MELODY_NAV_ON);
#endif
	return true;
}

/* Geofence alerts: hidden LoRa messages to the tracking group only. */
static void companion_send_geo_message(const char *text)
{
	ChannelDetails tracks_channel;
	const char *group_name = companion_mesh.prefs.tracking_group_name;

	if (!companion_get_public_channel(group_name, tracks_channel)) {
		LOG_WRN("geo: no %s channel", group_name);
		return;
	}
	if (!companion_send_hidden_group_message(tracks_channel, text)) {
		LOG_WRN("geo: unable to queue %s message", group_name);
		return;
	}
	LOG_INF("geo: queued %s message: %s", group_name, text);
}

/* ZEPHCORE: navigation feedback per fix — wrong-way ladder, arrival and the
 * periodic vContact report. Main thread, so headless boards get it too. */
static void companion_nav_fix_process(void)
{
	if (!companion_home.nav_active) {
		return;
	}
	struct gps_position pos = {};
	if (!gps_is_enabled() || !gps_get_last_known_position(&pos) || !pos.valid ||
	    (k_uptime_get() - pos.timestamp_ms) > 30000) {
		return;
	}
	double home_lat = 0, home_lon = 0;
	bool saved = false;
	const char *home_name = "";
	int home_index = 0;
	companion_home_get_point(&home_lat, &home_lon, &saved, &home_name, &home_index);
	double dist_m = tracking_distance_m(pos.latitude_ndeg, pos.longitude_ndeg,
		(int64_t)(home_lat * 1e9), (int64_t)(home_lon * 1e9));
	double bearing_deg = nav_bearing_deg(pos.latitude_ndeg / 1e9,
		pos.longitude_ndeg / 1e9, home_lat, home_lon);
	uint32_t now = k_uptime_get_32();
	/* nav.interval 0 = no vContact nav notices at all; beeps and STOP stay. */
	bool notify = companion_mesh.prefs.nav_report_interval_min != 0;

	/* Walked distance: only moving fixes count, as the tracking odometer
	 * does — GPS jitter at rest would otherwise accumulate. */
	if ((unsigned)((pos.speed_mms * 36U + 5000U) / 10000U) >
	    TRACKING_REPORT_STANDSTILL_KMH && companion_home.travel_has_last) {
		companion_home.travel_m += tracking_distance_m(
			companion_home.travel_last_lat_ndeg,
			companion_home.travel_last_lon_ndeg,
			pos.latitude_ndeg, pos.longitude_ndeg);
	}
	companion_home.travel_last_lat_ndeg = pos.latitude_ndeg;
	companion_home.travel_last_lon_ndeg = pos.longitude_ndeg;
	companion_home.travel_has_last = true;

	/* The ladder samples at the NAV screen cadence: per-fix deltas are GPS
	 * jitter and would keep resetting it. */
	if (companion_home.ladder_ms == 0 ||
	    (int32_t)(now - companion_home.ladder_ms) >= (int32_t)HOME_NAV_REFRESH_MS) {
		companion_home.ladder_ms = now;
		if (nav_wrongway_update(&companion_home.ww, dist_m)) {
			if (notify) {
				char rep[48], msg[160];

				nav_report_str(rep, sizeof(rep), dist_m, bearing_deg);
				snprintf(msg, sizeof(msg),
					 "Вы движетесь в неверном направлении!\n📍 %d-%s\n📏 %s",
					 home_index + 1, home_name, rep);
				companion_mesh.vcontactNotify(msg);
			}
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
			buzzer_play(MELODY_WRONG);
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DISPLAY) && IS_ENABLED(CONFIG_ZEPHCORE_UI_DESIGN_BUTTON)
			ui_pages_set_home_wrong_way(true);
#endif
		}
	}

	/* Arrived while walking to the point: success melody, then STOP. */
	if (dist_m < HOME_ARRIVAL_M) {
		char dur[16], dist[12], url[48], msg[MAX_TEXT_LEN + 1];
		size_t name_prefix = strlen(companion_mesh.prefs.node_name) + 2;

		tracking_duration_str(dur, sizeof(dur),
			(uint32_t)(now - companion_home.nav_started_ms));
		geofence_dist_str(dist, sizeof(dist), companion_home.travel_m);
		tracking_maps_url(url, sizeof(url), pos.latitude_ndeg / 1e9,
				  pos.longitude_ndeg / 1e9);
		if (notify) {
			nav_format_arrival(msg, sizeof(msg),
					   "🏁 Вы прибыли в точку назначения!",
					   home_index + 1, home_name, dur, dist, url);
			companion_mesh.vcontactNotify(msg);
		}
		if (companion_mesh.prefs.nav_telemetry_enabled) {
			nav_format_arrival(msg,
					   ((size_t)MAX_TEXT_LEN > name_prefix ?
						    MAX_TEXT_LEN - name_prefix : 0) + 1,
					   "🏁 Прибыл!",
					   home_index + 1, home_name, dur, dist, url);
			companion_send_nav_telemetry_message(msg);
		}
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
		buzzer_play(MELODY_ARRIVED);
#endif
		char reply[CLI_REPLY_SIZE];
		companion_nav_set_enabled(false, reply);
		return;
	}

	/* First fix after nav on, then every nav.interval minutes. */
	if (notify && (companion_home.report_ms == 0 ||
		       (int32_t)(now - companion_home.report_ms) >= 0)) {
		char rep[48], msg[96];

		companion_home.report_ms = now +
			(uint32_t)companion_mesh.prefs.nav_report_interval_min * 60000U;
		nav_report_str(rep, sizeof(rep), dist_m, bearing_deg);
		snprintf(msg, sizeof(msg), "📍 %d-%s\n📏 %s",
			 home_index + 1, home_name, rep);
		companion_mesh.vcontactNotify(msg);
	}

	/* The same report over LoRa to the tracking group at its own cadence,
	 * but only once the distance to the point has moved by 50 m — the
	 * tracking movement rule, so a parked navigator stays quiet. */
	if (companion_mesh.prefs.nav_telemetry_enabled &&
	    (companion_home.telemetry_ms == 0 ||
	     (int32_t)(now - companion_home.telemetry_ms) >= 0)) {
		float last = companion_home.report_last_dist_m;
		double delta = dist_m - (double)last;
		double moved = delta < 0.0 ? -delta : delta;

		companion_home.telemetry_ms = now +
			(uint32_t)companion_mesh.prefs.nav_telemetry_interval_min * 60000U;
		if (last < 0.0f || moved >= 50.0) {
			char rep[48], msg[160], url[48];

			companion_home.report_last_dist_m = (float)dist_m;
			nav_report_str(rep, sizeof(rep), dist_m, bearing_deg);
			tracking_maps_url(url, sizeof(url), pos.latitude_ndeg / 1e9,
					  pos.longitude_ndeg / 1e9);
			snprintf(msg, sizeof(msg),
				 "📍 %d-%s\n📏 %s\n%s",
				 home_index + 1, home_name, rep, url);
			companion_send_nav_telemetry_message(msg);
		}
	}
}

/* Geofence check per fresh fix: exit and return alerts (ADR 0011). Main
 * thread, event-driven like the nav feedback. */
static void companion_geo_fix_process(void)
{
	if (!(companion_geo.flags & GEO_FLAG_ENABLED) || companion_home.save_active) {
		return;
	}
	struct gps_position pos = {};
	if (!gps_is_enabled() || !gps_get_last_known_position(&pos) || !pos.valid ||
	    (k_uptime_get() - pos.timestamp_ms) > GEO_FIX_MAX_AGE_MS) {
		return;
	}
	double dist_m = tracking_distance_m(pos.latitude_ndeg, pos.longitude_ndeg,
		(int64_t)companion_mesh.prefs.geo_lat_e7 * 100,
		(int64_t)companion_mesh.prefs.geo_lon_e7 * 100);
	enum geofence_event ev = geofence_update(&companion_geo.fsm, dist_m,
		(double)companion_mesh.prefs.geo_radius_m);
	if (ev == GEO_EVENT_NONE) {
		return;
	}
	if (ev == GEO_EVENT_EXIT) {
		companion_mesh.prefs.geo_last_out_utc = rtc_clock.getCurrentTime();
	} else {
		companion_mesh.prefs.geo_last_in_utc = rtc_clock.getCurrentTime();
	}
	save_prefs_to_flash();
	char msg[MAX_TEXT_LEN + 1];
	/* sendGroupMessage prefixes "name: " and cuts the text at MAX_TEXT_LEN. */
	size_t name_prefix = strlen(companion_mesh.prefs.node_name) + 2;

	geofence_format_msg(msg,
			    ((size_t)MAX_TEXT_LEN > name_prefix ?
				     MAX_TEXT_LEN - name_prefix : 0) + 1,
			    ev == GEO_EVENT_EXIT,
			    (double)companion_mesh.prefs.geo_radius_m, dist_m,
			    pos.latitude_ndeg / 1e9, pos.longitude_ndeg / 1e9);
	companion_send_geo_message(msg);
	LOG_INF("geo: %s", msg);
}

/* ZEPHCORE: one "satellites lost" notice per outage; the LoRa reports resume
 * once the fix is back, and its distance counts as changed. */
static void companion_nav_link_check(void)
{
	if (!companion_home.nav_active ||
	    !companion_mesh.prefs.nav_telemetry_enabled) {
		return;
	}
	struct gps_position pos = {};
	bool fresh = gps_is_enabled() && gps_get_last_known_position(&pos) &&
		pos.valid && (k_uptime_get() - pos.timestamp_ms) <= 30000;

	if (fresh) {
		if (companion_home.link_lost_sent) {
			companion_home.link_lost_sent = false;
			companion_home.report_last_dist_m = -1.0f;
		}
		return;
	}
	if (companion_home.travel_has_last && !companion_home.link_lost_sent) {
		companion_home.link_lost_sent = true;
		companion_send_nav_telemetry_message(
			"🛰️ Связь со спутниками потеряна...");
	}
}

extern "C" bool companion_tracking_gps_control_allowed(void)
{
	return !companion_tracking.enabled;
}

static void companion_tracking_process(void)
{
	if (!companion_tracking.enabled) {
		return;
	}

	int64_t now = k_uptime_get();
	if (now < companion_tracking.next_report_ms) {
		return;
	}
	/* A delayed maintenance pass still begins the next full interval now. */
	companion_tracking.next_report_ms = now + companion_tracking_interval_ms();

	struct gps_position pos = {};
	struct gps_state_info gsi;
	gps_get_state_info(&gsi);
	bool has_fresh_coordinates = gps_get_last_known_position(&pos) &&
		pos.latitude_ndeg != 0 && pos.longitude_ndeg != 0 &&
		pos.timestamp_ms > companion_tracking.started_ms &&
		gsi.last_fix_age_s <= 30;
	if (!has_fresh_coordinates) {
		LOG_INF("tracking: interval skipped (no GPS fix)");
		return;
	}
	if (!companion_tracking_moved_min_distance(pos)) {
		LOG_INF("tracking: interval skipped (movement under 50 m)");
		return;
	}
	if (companion_send_tracking_message(pos)) {
		companion_tracking.has_last_sent_position = true;
		companion_tracking.last_sent_lat_ndeg = pos.latitude_ndeg;
		companion_tracking.last_sent_lon_ndeg = pos.longitude_ndeg;
	}
}

/* ========== Companion CLI ==========
 * The CommonCLI behind CompanionMesh::handleCommand, whose front-ends are the
 * app's CMD_RUN_CLI_COMMAND, the USB text console (the '<' sync byte tells V3
 * frames from text; BLE NUS has no such marker, so it has no text console),
 * and over-the-air TXT_TYPE_CLI_COMMAND. Not gated on ZEPHCORE_USB_STACK. */

/* No reply held, and every transport's TX drained. */
static bool companion_transport_tx_idle(void)
{
	if (companion_serial.holding()) {
		return false;
	}
#if IS_ENABLED(CONFIG_BT)
	if (!ble_interface.txIdle()) {
		return false;
	}
#endif
#if ZEPHCORE_USB_STACK
	if (!usb_interface.txIdle()) {
		return false;
	}
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_TRANSPORT_TCP)
	if (!tcp_interface.txIdle()) {
		return false;
	}
#endif
	return true;
}

static CompanionCLICallbacks companion_cli_cbs(data_store, companion_mesh, lora_radio, zephyr_board,
					       ms_clock, packet_mgr, companion_transport_tx_idle);
/* No region map and no client ACL: a companion has neither (nullptr both). */
static CommonCLI companion_cli(zephyr_board, rtc_clock, sensors, nullptr, nullptr,
			       &companion_mesh.prefs, &companion_cli_cbs);

/* CMD_SET_RADIO_PARAMS / CMD_SET_RADIO_TX_POWER (needs companion_cli, hence
 * here). Unlike the CLI's `set radio` this applies live, so the radio is
 * already on the new preset when the CAD state resets: preset_pending false. */
static void radio_reconfigure(bool preset_changed)
{
	lora_radio.reconfigure();

	if (preset_changed) {
		companion_cli.resetCadState(false);
	}
}

#if defined(CONFIG_ZEPHCORE_AUTO_SHUTDOWN_MILLIVOLTS) && \
	CONFIG_ZEPHCORE_AUTO_SHUTDOWN_MILLIVOLTS > 0
#define ZEPHCORE_HAS_AUTO_SHUTDOWN 1
#else
#define ZEPHCORE_HAS_AUTO_SHUTDOWN 0
#endif

/* ========== Low-battery policy (app/PowerPolicy.cpp) ==========
 * Auto-shutdown and the v-contact alert, on every companion that reads a
 * battery. These hooks are its only v-contact side. */

/* Just before a low-battery power-off: with an app connected, send the notice
 * live (the policy then waits a short grace). Otherwise the policy stores the
 * reason for the next boot, since the offline queue does not survive System
 * OFF. */
static bool power_notify_shutdown(uint16_t battery_mv)
{
	bool queued = false;
	if (companion_mesh.prefs.auto_shutdown_emergency) {
		queued = companion_send_auto_shutdown_emergency(
			battery_mv, (uint32_t)k_uptime_get());
	}
	if (companion_transport_up() && companion_mesh.isVContactEnabled()) {
		companion_mesh.vcontactNotify("Выключение: низкий заряд");
		queued = true;
	}
	return queued;
}

static void power_battery_alert(uint16_t mv, uint16_t threshold_mv)
{
	char msg[48];

	snprintf(msg, sizeof(msg), "Заряд низкий: %u мВ (порог %u мВ)", mv, threshold_mv);
	companion_mesh.vcontactNotify(msg);
}

static bool power_alert_enabled(void)
{
	return companion_mesh.isVContactEnabled();
}

static void power_prefs_dirty(void)
{
	k_event_post(&mesh_events, MESH_EVENT_PREFS_DIRTY);
}

static PowerPolicy power_policy(zephyr_board, companion_mesh.prefs,
				{power_notify_shutdown, power_battery_alert, power_alert_enabled,
				 power_prefs_dirty});

extern "C" bool ui_shutdown_in_progress(void)
{
#ifdef ZEPHCORE_LORA
	return power_policy.isShuttingDown() || atomic_get(&manual_shutdown_requested) != 0;
#else
	return power_policy.isShuttingDown();
#endif
}

static void power_policy_tick(void)
{
	power_policy.tick();
}

static const char *companion_cli_help(const char *line)
{
	return local_cli_help(LocalCLIHelpRole::Companion, line);
}

/* Command-line settings for the notification-only v-contact. */
static bool handle_vcontact_settings_cli(const char *line, char *reply)
{
	if (strcmp(line, "get v.contact") == 0) {
		snprintf(reply, CLI_REPLY_SIZE, "v.contact: %s",
			 companion_mesh.prefs.v_contact_enabled ? "on" : "off");
		return true;
	}
	if (strncmp(line, "set v.contact ", 14) == 0) {
		const char *arg = line + 14;
		while (*arg == ' ') arg++;
		bool en;
		if (strcmp(arg, "on") == 0 || strcmp(arg, "1") == 0) {
			en = true;
		} else if (strcmp(arg, "off") == 0 || strcmp(arg, "0") == 0) {
			en = false;
		} else {
			strcpy(reply, "ERROR: use on|off|1|0");
			return true;
		}
		bool was = companion_mesh.prefs.v_contact_enabled != 0;
		companion_mesh.prefs.v_contact_enabled = en ? 1 : 0;
		k_event_post(&mesh_events, MESH_EVENT_PREFS_DIRTY);
		if (en && !was) {
			companion_mesh.vcontactPushAdvert();
		} else if (!en && was) {
			companion_mesh.vcontactPushDeleted();
			/* The app drops the contact, and its flags with it. */
			companion_mesh.prefs.v_contact_flags = 0;
		}
		snprintf(reply, CLI_REPLY_SIZE, "OK - v.contact %s", en ? "on" : "off");
		return true;
	}
	if (strcmp(line, "get v.batteryalert") == 0) {
		uint16_t pref = companion_mesh.prefs.v_battery_alert_mv;
		if (pref == 0) {
			strcpy(reply, "v.batteryalert: off");
		} else if (pref == 0xFFFF) {
			snprintf(reply, CLI_REPLY_SIZE, "v.batteryalert: default (%u mV)",
				 power_policy.alertThresholdMv());
		} else {
			snprintf(reply, CLI_REPLY_SIZE, "v.batteryalert: %u mV", pref);
		}
		return true;
	}
	if (strncmp(line, "set v.batteryalert ", 19) == 0) {
		const char *arg = line + 19;
		while (*arg == ' ') arg++;
		uint16_t v;
		if (strncmp(arg, "default", 7) == 0) {
			v = 0xFFFF;
		} else {
			/* Numbers only, same validation as autoshutdown. */
			char *end = NULL;
			long n = strtol(arg, &end, 10);
			while (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t') {
				end++;
			}
			if (arg[0] < '0' || arg[0] > '9' || *end != '\0' || n > 5000) {
				strcpy(reply, "ERROR: use 0 (off), 1-5000 mV, or default");
				return true;
			}
			v = (uint16_t)n;
		}
		companion_mesh.prefs.v_battery_alert_mv = v;
		k_event_post(&mesh_events, MESH_EVENT_PREFS_DIRTY);
		power_policy.rearmAlert();
		if (v == 0) {
			strcpy(reply, "OK - v.batteryalert off");
		} else if (v == 0xFFFF) {
			snprintf(reply, CLI_REPLY_SIZE, "OK - v.batteryalert default (%u mV)",
				 power_policy.alertThresholdMv());
		} else {
			snprintf(reply, CLI_REPLY_SIZE, "OK - v.batteryalert %u mV", v);
		}
		return true;
	}
	return false;
}

static bool handle_autoshutdown_emergency_cli(const char *line, char *reply)
{
	if (strcmp(line, "get autoshutdown.emergency") == 0) {
		snprintf(reply, CLI_REPLY_SIZE, "autoshutdown.emergency: %s",
			 companion_mesh.prefs.auto_shutdown_emergency ? "on" : "off");
		return true;
	}
	if (strncmp(line, "set autoshutdown.emergency ", 27) != 0) return false;
	const char *arg = line + 27;
	while (*arg == ' ' || *arg == '\t') arg++;
	if (strcmp(arg, "on") == 0 || strcmp(arg, "1") == 0) {
		companion_mesh.prefs.auto_shutdown_emergency = 1;
	} else if (strcmp(arg, "off") == 0 || strcmp(arg, "0") == 0) {
		companion_mesh.prefs.auto_shutdown_emergency = 0;
	} else {
		strcpy(reply, "ERROR: use on|off|1|0");
		return true;
	}
	k_event_post(&mesh_events, MESH_EVENT_PREFS_DIRTY);
	snprintf(reply, CLI_REPLY_SIZE, "OK - autoshutdown.emergency %s",
		 companion_mesh.prefs.auto_shutdown_emergency ? "on" : "off");
	return true;
}

static bool handle_uptime_cli(const char *line, char *reply)
{
	if (strcmp(line, "uptime") != 0 && strcmp(line, "get uptime") != 0) {
		return false;
	}
	uint64_t seconds = (uint64_t)k_uptime_get() / 1000U;
	snprintf(reply, CLI_REPLY_SIZE, "uptime: %llud %02lluh %02llum %02llus",
		 (unsigned long long)(seconds / 86400U),
		 (unsigned long long)((seconds / 3600U) % 24U),
		 (unsigned long long)((seconds / 60U) % 60U),
		 (unsigned long long)(seconds % 60U));
	return true;
}

static bool handle_tz_cli(const char *line, char *reply)
{
	if (strcmp(line, "tz") != 0) {
		return false;
	}

	int offset = ui_get_timezone_offset_minutes();
	char sign = offset < 0 ? '-' : '+';
	int abs_minutes = offset < 0 ? -offset : offset;
	int hours = abs_minutes / 60;
	int minutes = abs_minutes % 60;

	if (minutes == 0) {
		snprintf(reply, CLI_REPLY_SIZE, "tz: GMT%c%d", sign, hours);
	} else {
		snprintf(reply, CLI_REPLY_SIZE, "tz: GMT%c%d:%02d", sign, hours, minutes);
	}
	return true;
}

static bool handle_sos_cli(const char *line, char *reply, bool silent)
{
	if (strcmp(line, "sos") != 0) {
		return false;
	}
	return companion_sos_request(reply, silent);
}

#if defined(ZEPHCORE_FALL_DETECTOR)
static bool handle_fall_cli(const char *line, char *reply)
{
	if (strcmp(line, "fall") == 0 || strcmp(line, "get fall") == 0) {
		snprintf(reply, CLI_REPLY_SIZE, "fall: %s; sens=%u",
			 fall_detector_is_enabled() ? "on" : "off",
			 fall_detector_get_sensitivity());
		return true;
	}
	if (strcmp(line, "fall on") == 0) {
		if (!fall_detector_set_enabled(true)) {
			strcpy(reply, "ERROR: fall detector unavailable");
			return true;
		}
		companion_mesh.prefs.fall_enabled = 1;
		k_event_post(&mesh_events, MESH_EVENT_PREFS_DIRTY);
		snprintf(reply, CLI_REPLY_SIZE, "OK - fall on (sens %u)",
			 fall_detector_get_sensitivity());
		return true;
	}
	if (strcmp(line, "fall off") == 0) {
		fall_detector_set_enabled(false);
		companion_mesh.prefs.fall_enabled = 0;
		k_event_post(&mesh_events, MESH_EVENT_PREFS_DIRTY);
		if (companion_sos.type == COMPANION_EMERGENCY_FALL) {
			companion_fall_alarm_stop();
			companion_fall_cancel_pending();
		}
		strcpy(reply, "OK - fall off");
		return true;
	}
	if (strcmp(line, "get fall.sens") == 0) {
		snprintf(reply, CLI_REPLY_SIZE, "fall.sens: %u (1=max, 3=normal, 5=strict)",
			 fall_detector_get_sensitivity());
		return true;
	}
	if (strncmp(line, "set fall.sens ", 14) != 0) {
		return false;
	}
	const char *arg = line + 14;
	while (*arg == ' ' || *arg == '\t') {
		arg++;
	}
	if (arg[0] < '1' || arg[0] > '5' ||
	    (arg[1] != '\0' && arg[1] != '\r' && arg[1] != '\n' &&
	     arg[1] != ' ' && arg[1] != '\t')) {
		strcpy(reply, "ERROR: use a value from 1 to 5");
		return true;
	}
	uint8_t value = (uint8_t)(arg[0] - '0');
	if (!fall_detector_set_sensitivity(value)) {
		strcpy(reply, "ERROR: fall detector unavailable");
		return true;
	}
	companion_mesh.prefs.fall_sensitivity = value;
	k_event_post(&mesh_events, MESH_EVENT_PREFS_DIRTY);
	snprintf(reply, CLI_REPLY_SIZE, "OK - fall.sens %u", value);
	return true;
}
#endif

static bool handle_tracking_cli(const char *line, char *reply, bool silent)
{
	if (companion_tracking.enabled &&
		(strcmp(line, "gps off") == 0 ||
		 strncmp(line, "set gps duty ", 13) == 0)) {
		strcpy(reply, "ERROR: tracking is active; use tracking off");
		return true;
	}
	if (strcmp(line, "wp") == 0 || strncmp(line, "wp ", 3) == 0) {
		if (strcmp(line, "wp clear") == 0) {
			/* Point 1 (GPS HOME) is never renamed or reset. */
			for (int i = 1; i < 10; i++) {
				companion_mesh.prefs.home_lat[i] = ZC_POINT_DEFAULT_LAT_E7 / 1e7;
				companion_mesh.prefs.home_lon[i] = ZC_POINT_DEFAULT_LON_E7 / 1e7;
				companion_mesh.prefs.home_saved[i] = 0;
				strcpy(companion_mesh.prefs.home_name[i], "FREE");
			}
			save_prefs_to_flash();
			ui_request_render();
			strcpy(reply, "OK - wp clear");
			return true;
		}
		if (strcmp(line, "wp") == 0 || strcmp(line, "wp list") == 0) {
			int n = 0;
			for (int i = 0; i < 10; i++) {
				n += snprintf(reply + n, CLI_REPLY_SIZE - n, "%c%d:%s",
					      i == companion_home.selected ? '*' : ' ',
					      i + 1, companion_mesh.prefs.home_name[i]);
				if (n >= CLI_REPLY_SIZE - 1) break;
			}
		} else if (strncmp(line, "wp save", 7) == 0 &&
			   (line[7] == '\0' || line[7] == ' ')) {
			int slot;
			char name[12];
			const char *err;

			if (!gps_is_available()) {
				strcpy(reply, "ERROR: GPS unavailable");
				return true;
			}
			if (companion_home.nav_active) {
				strcpy(reply, "ERROR: navigation is on");
				return true;
			}
			if (companion_home.save_active) {
				strcpy(reply, "ERROR: saving home point");
				return true;
			}
			err = companion_wp_save_parse(line + 7, companion_home.selected,
						      &slot, name, sizeof(name));
			if (err != NULL) {
				strcpy(reply, err);
				return true;
			}
			companion_home.selected = slot;
			if (name[0] != '\0') {
				/* The name lands now; the coordinates wait for a fix. */
				strcpy(companion_mesh.prefs.home_name[slot], name);
				k_event_post(&mesh_events, MESH_EVENT_PREFS_DIRTY);
			}
			companion_home_start_save(false);
			if (name[0] != '\0') {
				snprintf(reply, CLI_REPLY_SIZE,
					 "OK - wp %d %s: waiting for fix",
					 slot + 1, name);
			} else {
				snprintf(reply, CLI_REPLY_SIZE,
					 "OK - wp %d: waiting for fix", slot + 1);
			}
			return true;
		} else if (strncmp(line, "wp name", 7) == 0 &&
			   (line[7] == '\0' || line[7] == ' ')) {
			int slot;
			char name[12];
			const char *err;

			err = companion_wp_save_parse(line + 7, companion_home.selected,
						      &slot, name, sizeof(name));
			if (err != NULL) {
				strcpy(reply, err);
				return true;
			}
			if (name[0] == '\0') {
				strcpy(reply, "Error: wp name [n] <name>");
				return true;
			}
			strcpy(companion_mesh.prefs.home_name[slot], name);
			k_event_post(&mesh_events, MESH_EVENT_PREFS_DIRTY);
			ui_request_render();
			snprintf(reply, CLI_REPLY_SIZE, "OK - wp name %d %s",
				 slot + 1, name);
			return true;
		} else {
			long v = strtol(line + 3, nullptr, 10);
			if (v >= 1 && v <= 10) {
				companion_home.selected = (int)v - 1;
				ui_request_render();
				snprintf(reply, CLI_REPLY_SIZE, "OK - wp %ld %s  %.5f,%.5f",
					 v, companion_mesh.prefs.home_name[companion_home.selected],
					 companion_mesh.prefs.home_lat[companion_home.selected],
					 companion_mesh.prefs.home_lon[companion_home.selected]);
			} else {
				strcpy(reply, "Error: wp 1..10");
			}
		}
		return true;
	}
	if (strcmp(line, "nav") == 0 || strcmp(line, "nav start") == 0 ||
	    strcmp(line, "nav stop") == 0) {
		if (strcmp(line, "nav start") == 0) {
			companion_nav_set_enabled(true, reply);
		} else if (strcmp(line, "nav stop") == 0) {
			companion_nav_set_enabled(false, reply);
		} else if (!companion_home.nav_active) {
			snprintf(reply, CLI_REPLY_SIZE, "nav: off  %d-%s",
				 companion_home.selected + 1,
				 companion_mesh.prefs.home_name[companion_home.selected]);
		} else {
			struct gps_position pos = {};
			double home_lat = 0, home_lon = 0;
			bool saved = false;
			const char *home_name = "";
			int home_index = 0;

			companion_home_get_point(&home_lat, &home_lon, &saved,
						 &home_name, &home_index);
			if (gps_is_enabled() && gps_get_last_known_position(&pos) &&
			    pos.valid &&
			    (k_uptime_get() - pos.timestamp_ms) <= 30000) {
				char dstr[16];
				nav_distance_str(dstr, sizeof(dstr),
					tracking_distance_m(pos.latitude_ndeg,
						pos.longitude_ndeg,
						(int64_t)(home_lat * 1e9),
						(int64_t)(home_lon * 1e9)));
				snprintf(reply, CLI_REPLY_SIZE, "nav: on  %d-%s  %s  %s",
					 home_index + 1, home_name, dstr,
					 nav_cardinal_en(nav_bearing_deg(
						pos.latitude_ndeg / 1e9,
						pos.longitude_ndeg / 1e9,
						home_lat, home_lon)));
			} else {
				snprintf(reply, CLI_REPLY_SIZE, "nav: on  %d-%s  ?? km  ?",
					 home_index + 1, home_name);
			}
		}
		return true;
	}
	if (strcmp(line, "get nav.interval") == 0) {
		if (companion_mesh.prefs.nav_report_interval_min == 0) {
			strcpy(reply, "nav.interval: off");
		} else {
			snprintf(reply, CLI_REPLY_SIZE, "nav.interval: %u min",
				 companion_mesh.prefs.nav_report_interval_min);
		}
		return true;
	}
	if (strncmp(line, "set nav.interval ", 17) == 0) {
		const char *arg = line + 17;
		while (*arg == ' ') {
			arg++;
		}
		char *end = NULL;
		unsigned long minutes = strtoul(arg, &end, 10);
		while (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t') {
			end++;
		}
		if (arg[0] < '0' || arg[0] > '9' || *end != '\0' ||
		    minutes > 1440) {
			strcpy(reply, "ERROR: interval must be 0..1440 minutes");
			return true;
		}
		companion_mesh.prefs.nav_report_interval_min = (uint16_t)minutes;
		if (companion_home.nav_active) {
			companion_home.report_ms = (uint32_t)k_uptime_get() +
				(uint32_t)minutes * 60000U;
		}
		k_event_post(&mesh_events, MESH_EVENT_PREFS_DIRTY);
		if (minutes == 0) {
			strcpy(reply, "OK - nav.interval off");
		} else {
			snprintf(reply, CLI_REPLY_SIZE, "OK - nav.interval %lu min",
				 minutes);
		}
		return true;
	}
	if (strcmp(line, "nav telemetry") == 0 ||
	    strcmp(line, "nav telemetry on") == 0 ||
	    strcmp(line, "nav telemetry off") == 0) {
		if (strcmp(line, "nav telemetry on") == 0) {
			companion_mesh.prefs.nav_telemetry_enabled = 1;
			k_event_post(&mesh_events, MESH_EVENT_PREFS_DIRTY);
			strcpy(reply, "OK - nav telemetry on");
		} else if (strcmp(line, "nav telemetry off") == 0) {
			companion_mesh.prefs.nav_telemetry_enabled = 0;
			k_event_post(&mesh_events, MESH_EVENT_PREFS_DIRTY);
			strcpy(reply, "OK - nav telemetry off");
		} else {
			snprintf(reply, CLI_REPLY_SIZE, "nav.telemetry: %s (%u min)",
				 companion_mesh.prefs.nav_telemetry_enabled ?
					"on" : "off",
				 companion_mesh.prefs.nav_telemetry_interval_min);
		}
		return true;
	}
	if (strcmp(line, "get nav.telemetry.interval") == 0) {
		snprintf(reply, CLI_REPLY_SIZE, "nav.telemetry.interval: %u min",
			 companion_mesh.prefs.nav_telemetry_interval_min);
		return true;
	}
	if (strncmp(line, "set nav.telemetry.interval ", 26) == 0) {
		const char *arg = line + 26;
		while (*arg == ' ') {
			arg++;
		}
		char *end = NULL;
		unsigned long minutes = strtoul(arg, &end, 10);
		while (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t') {
			end++;
		}
		if (arg[0] < '0' || arg[0] > '9' || *end != '\0' ||
		    minutes < TRACKING_MIN_INTERVAL_MIN || minutes > UINT16_MAX) {
			snprintf(reply, CLI_REPLY_SIZE,
				 "ERROR: interval must be %u minutes or more",
				 TRACKING_MIN_INTERVAL_MIN);
			return true;
		}
		companion_mesh.prefs.nav_telemetry_interval_min = (uint16_t)minutes;
		if (companion_home.nav_active) {
			companion_home.telemetry_ms = (uint32_t)k_uptime_get() +
				(uint32_t)minutes * 60000U;
		}
		k_event_post(&mesh_events, MESH_EVENT_PREFS_DIRTY);
		snprintf(reply, CLI_REPLY_SIZE, "OK - nav.telemetry.interval %lu min",
			 minutes);
		return true;
	}
	if (strcmp(line, "tracking") == 0) {
		snprintf(reply, CLI_REPLY_SIZE, "tracking: %s (%u min)",
			 companion_tracking.enabled ? "on" : "off",
			 companion_mesh.prefs.tracking_interval_minutes);
		return true;
	}
	if (strcmp(line, "tracking on") == 0) {
		(void)companion_tracking_set_enabled(true, reply, silent);
		return true;
	}
	if (strcmp(line, "tracking off") == 0) {
		(void)companion_tracking_set_enabled(false, reply, silent);
		return true;
	}
	if (strcmp(line, "get tracking.interval") == 0) {
		snprintf(reply, CLI_REPLY_SIZE, "tracking.interval: %u min",
			 companion_mesh.prefs.tracking_interval_minutes);
		return true;
	}
	if (strcmp(line, "get tracking.group") == 0) {
		snprintf(reply, CLI_REPLY_SIZE, "tracking.group: %s",
			 companion_mesh.prefs.tracking_group_name);
		return true;
	}
	if (strncmp(line, "set tracking.group ", 19) == 0) {
		const char *arg = line + 19;
		while (*arg == ' ' || *arg == '\t') {
			arg++;
		}
		size_t len = strlen(arg);
		while (len > 0 && (arg[len - 1] == ' ' || arg[len - 1] == '\t')) {
			len--;
		}
		bool add_hash = len > 0 && arg[0] != '#';
		if (len == 0 || len + (add_hash ? 1 : 0) >=
		    sizeof(companion_mesh.prefs.tracking_group_name)) {
			strcpy(reply, "ERROR: group name must be 1-31 bytes");
			return true;
		}
		for (size_t i = 0; i < len; i++) {
			if (arg[i] == '\r' || arg[i] == '\n') {
				strcpy(reply, "ERROR: invalid group name");
				return true;
			}
		}
		char group_name[sizeof(companion_mesh.prefs.tracking_group_name)] = {};
		size_t group_len = len;
		if (add_hash) {
			group_name[0] = '#';
			memcpy(&group_name[1], arg, len);
			group_len++;
		} else {
			memcpy(group_name, arg, len);
		}
		group_name[group_len] = '\0';
		ChannelDetails channel;
		if (!companion_get_public_channel(group_name, channel)) {
			strcpy(reply, "ERROR: unable to create tracking group");
			return true;
		}
		memcpy(companion_mesh.prefs.tracking_group_name, group_name,
		       sizeof(group_name));
		k_event_post(&mesh_events, MESH_EVENT_PREFS_DIRTY);
		snprintf(reply, CLI_REPLY_SIZE, "OK - tracking.group %s",
			 companion_mesh.prefs.tracking_group_name);
		return true;
	}
	if (strncmp(line, "set tracking.interval ", 22) == 0) {
		const char *arg = line + 22;
		while (*arg == ' ') {
			arg++;
		}
		char *end = NULL;
		unsigned long minutes = strtoul(arg, &end, 10);
		while (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t') {
			end++;
		}
		if (arg[0] < '0' || arg[0] > '9' || *end != '\0' ||
			minutes < TRACKING_MIN_INTERVAL_MIN || minutes > UINT16_MAX) {
			snprintf(reply, CLI_REPLY_SIZE, "ERROR: interval must be %u minutes or more",
				 TRACKING_MIN_INTERVAL_MIN);
			return true;
		}
		companion_mesh.prefs.tracking_interval_minutes = (uint16_t)minutes;
		if (companion_tracking.enabled) {
			companion_tracking.next_report_ms = k_uptime_get() +
				companion_tracking_interval_ms();
		}
		k_event_post(&mesh_events, MESH_EVENT_PREFS_DIRTY);
		companion_tracking_ui_update();
		snprintf(reply, CLI_REPLY_SIZE, "OK - tracking.interval %lu min", minutes);
		return true;
	}
	return false;
}

/* "50m", "999m", "1km", "2.5km" — the zone radius as the geo reply shows it. */
static void geo_radius_str(char *buf, size_t len, unsigned meters)
{
	if (meters < 1000U) {
		snprintf(buf, len, "%um", meters);
		return;
	}
	unsigned hundredths = (meters + 5U) / 10U; /* km x 100 */
	unsigned frac = hundredths % 100U;

	if (frac == 0) {
		snprintf(buf, len, "%ukm", hundredths / 100U);
	} else if (frac % 10U == 0) {
		snprintf(buf, len, "%u.%ukm", hundredths / 100U, frac / 10U);
	} else {
		snprintf(buf, len, "%u.%02ukm", hundredths / 100U, frac);
	}
}

/* "22.12.2006 17:44:22" — UTC, like the clock command. */
static void geo_format_utc(char *buf, size_t len, uint32_t epoch)
{
	time_t t = (time_t)epoch;
	struct tm tmv;

	gmtime_r(&t, &tmv);
	snprintf(buf, len, "%02d.%02d.%04d %02d:%02d:%02d",
		 tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_year + 1900,
		 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

/* Geofence CLI (ADR 0011). geo save is local/app only — the remote allowlist
 * keeps it out; set geo.point is the remote coordinate path. */
static bool handle_geo_cli(const char *line, char *reply)
{
	bool geo_on = (companion_geo.flags & GEO_FLAG_ENABLED) != 0;

	if (geo_on &&
	    (strcmp(line, "gps off") == 0 ||
	     strncmp(line, "set gps duty ", 13) == 0)) {
		strcpy(reply, "ERROR: geo is on; use geo off");
		return true;
	}
	if (strcmp(line, "geo") == 0 || strcmp(line, "geo on") == 0 ||
	    strcmp(line, "geo off") == 0) {
		if (strcmp(line, "geo") != 0) {
			return companion_geo_set_enabled(strcmp(line, "geo on") == 0,
							 reply);
		}
		char r[12];
		const char *zone = "NOFIX";
		struct gps_position pos = {};

		if (gps_is_enabled() && gps_get_last_known_position(&pos) &&
		    pos.valid &&
		    (k_uptime_get() - pos.timestamp_ms) <= GEO_FIX_MAX_AGE_MS) {
			double dist_m = tracking_distance_m(
				pos.latitude_ndeg, pos.longitude_ndeg,
				(int64_t)companion_mesh.prefs.geo_lat_e7 * 100,
				(int64_t)companion_mesh.prefs.geo_lon_e7 * 100);
			zone = dist_m > (double)companion_mesh.prefs.geo_radius_m ?
				       "OUTSIDE" : "INSIDE";
		}
		geo_radius_str(r, sizeof(r), companion_mesh.prefs.geo_radius_m);
		snprintf(reply, CLI_REPLY_SIZE, "geo: %s %s %s %.5f,%.5f",
			 geo_on ? "on" : "off", zone, r,
			 companion_mesh.prefs.geo_lat_e7 / 1e7,
			 companion_mesh.prefs.geo_lon_e7 / 1e7);
		return true;
	}
	if (strcmp(line, "geo save") == 0) {
		if (!gps_is_available()) {
			strcpy(reply, "ERROR: GPS unavailable");
			return true;
		}
		if (companion_home.nav_active) {
			strcpy(reply, "ERROR: navigation is on");
			return true;
		}
		if (companion_home.save_active) {
			strcpy(reply, "ERROR: saving point");
			return true;
		}
		if (companion_tracking.enabled) {
			strcpy(reply, "ERROR: tracking is active; use tracking off");
			return true;
		}
		companion_home_start_save(true);
		strcpy(reply, "OK - geo save: waiting for fix");
		return true;
	}
	if (strcmp(line, "geo log") == 0) {
		char in_s[24] = "-", out_s[24] = "-";

		if (companion_mesh.prefs.geo_last_in_utc != 0) {
			geo_format_utc(in_s, sizeof(in_s),
				       companion_mesh.prefs.geo_last_in_utc);
		}
		if (companion_mesh.prefs.geo_last_out_utc != 0) {
			geo_format_utc(out_s, sizeof(out_s),
				       companion_mesh.prefs.geo_last_out_utc);
		}
		snprintf(reply, CLI_REPLY_SIZE, "last in: %s; out: %s", in_s, out_s);
		return true;
	}
	if (strcmp(line, "get geo.point") == 0) {
		snprintf(reply, CLI_REPLY_SIZE, "geo.point: %.5f,%.5f (%s)",
			 companion_mesh.prefs.geo_lat_e7 / 1e7,
			 companion_mesh.prefs.geo_lon_e7 / 1e7,
			 companion_mesh.prefs.geo_saved ? "saved" : "default");
		return true;
	}
	if (strncmp(line, "set geo.point ", 14) == 0) {
		double lat, lon;

		if (!geofence_parse_point(line + 14, &lat, &lon)) {
			strcpy(reply, "usage: set geo.point <lat>,<lon>  e.g. set geo.point 43.241117,76.927301");
			return true;
		}
		companion_mesh.prefs.geo_lat_e7 = (int32_t)llround(lat * 1e7);
		companion_mesh.prefs.geo_lon_e7 = (int32_t)llround(lon * 1e7);
		companion_mesh.prefs.geo_saved = 1;
		save_prefs_to_flash();
		if (geo_on) {
			geofence_reset(&companion_geo.fsm);
		}
		snprintf(reply, CLI_REPLY_SIZE, "OK - geo.point %.5f,%.5f",
			 companion_mesh.prefs.geo_lat_e7 / 1e7,
			 companion_mesh.prefs.geo_lon_e7 / 1e7);
		return true;
	}
	if (strcmp(line, "get geo.radius") == 0) {
		snprintf(reply, CLI_REPLY_SIZE, "geo.radius: %u m (50..50000)",
			 companion_mesh.prefs.geo_radius_m);
		return true;
	}
	if (strncmp(line, "set geo.radius ", 15) == 0) {
		const char *arg = line + 15;
		char *end = NULL;
		long meters = strtol(arg, &end, 10);

		while (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t') {
			end++;
		}
		if (arg[0] < '0' || arg[0] > '9' || *end != '\0' ||
		    meters < (long)GEO_RADIUS_M_MIN || meters > (long)GEO_RADIUS_M_MAX) {
			strcpy(reply, "usage: set geo.radius <meters 50..50000>");
			return true;
		}
		companion_mesh.prefs.geo_radius_m = (uint16_t)meters;
		save_prefs_to_flash();
		if (geo_on) {
			geofence_reset(&companion_geo.fsm);
		}
		snprintf(reply, CLI_REPLY_SIZE, "OK - geo.radius %u m", (unsigned)meters);
		return true;
	}
	return false;
}

/* Offgrid is deliberately a runtime switch: a reboot always starts a client
 * in its normal non-forwarding mode.  The same CLI executor services USB and
 * vContact, so no separate remote-command implementation is needed. */
static bool handle_offgrid_cli(const char *line, char *reply)
{
	if (strcmp(line, "offgrid") == 0) {
		snprintf(reply, CLI_REPLY_SIZE, "offgrid: %s",
			 companion_mesh.isOffgridActive() ? "on" : "off");
		return true;
	}

	bool enable;
	if (strcmp(line, "offgrid on") == 0) {
		enable = true;
	} else if (strcmp(line, "offgrid off") == 0) {
		enable = false;
	} else {
		return false;
	}

	mesh::OffgridResult result = companion_mesh.setOffgridMode(enable);
	if (result == mesh::OFFGRID_NO_BAND) {
		strcpy(reply, "ERROR: no off-grid frequency for this band");
		return true;
	}
	ui_set_offgrid_mode(companion_mesh.isOffgridActive());
	snprintf(reply, CLI_REPLY_SIZE, "OK - offgrid %s (%.3f MHz)",
		 companion_mesh.isOffgridActive() ? "on" : "off",
		 (double)companion_mesh.prefs.freq);
	return true;
}

/* Locate this companion with a five-second audible melody.  This explicit
 * request deliberately overrides user mute and the low-battery sound limit. */
static bool handle_findme_cli(const char *line, char *reply)
{
	if (strcmp(line, "findme") != 0) {
		return false;
	}

#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
	buzzer_play_force(MELODY_FINDME);
	strcpy(reply, "OK - findme melody (5 s)");
#else
	strcpy(reply, "ERROR: no buzzer on this board");
#endif
	return true;
}



/* The CompanionMesh::handleCommand callback runs on the main thread. Remote
 * output is rendered into a local full-size buffer, then packet-bounded. */
static_assert(COMPANION_CLI_REPLY_SIZE == CLI_REPLY_SIZE,
	      "companion CLI reply buffer must match CommonCLI reply size");

static void companion_cli_notify(const char *line, const char *reply)
{
	const char *notice = nullptr;
	if (strcmp(line, "sos") == 0) {
		if (strncmp(reply, "ERROR", 5) != 0) notice = "SOS активирован";
	} else if (strcmp(reply, "ok") == 0) {
		if (strcmp(line, "gps on") == 0) notice = "GPS: вкл";
		else if (strcmp(line, "gps off") == 0) notice = "GPS: выкл";
	} else if (strcmp(reply, "OK") == 0) {
		if (strcmp(line, "leds on") == 0) notice = "LEDs: вкл";
		else if (strcmp(line, "leds off") == 0) notice = "LEDs: выкл";
		else if (strcmp(line, "buzz on") == 0) notice = "Буззер: вкл";
		else if (strcmp(line, "buzz off") == 0) notice = "Буззер: выкл";
	}
	if (notice != nullptr) companion_mesh.vcontactNotify(notice);
}

static void companion_cli_exec(const char *line, uint32_t sender_timestamp,
			       uint8_t reply_hdr_used, char *reply)
{
	const char *requested_line = line;
	bool silent_notifications = companion_cli_silent_notifications(sender_timestamp);
	reply[0] = '\0';
	if (strcmp(line, "gps diag") == 0 ||
	    strcmp(line, "get gps diag") == 0 ||
	    strcmp(line, "set gps diag") == 0 ||
	    strncmp(line, "set gps diag ", 13) == 0 ||
	    strcmp(line, "get multi.acks") == 0 ||
	    strcmp(line, "set multi.acks") == 0 ||
	    strncmp(line, "set multi.acks ", 15) == 0 ||
	    strncmp(line, "set leds ", 9) == 0 ||
	    strncmp(line, "set buzzer ", 11) == 0) return;
#if !IS_ENABLED(CONFIG_ZEPHCORE_UI_DISPLAY)
	if (strcmp(line, "get display.rotate") == 0 ||
	    strcmp(line, "set display.rotate") == 0 ||
	    strncmp(line, "set display.rotate ", 19) == 0 ||
	    strcmp(line, "get input.rotate") == 0 ||
	    strcmp(line, "set input.rotate") == 0 ||
	    strncmp(line, "set input.rotate ", 17) == 0) return;
#endif
	const char *aliased = companion_cli_alias_command(line);
	if (aliased != nullptr) line = aliased;
	if (strcmp(line, "shutdown") == 0) {
		strcpy(reply, "To confirm use shutdown y");
		return;
	}
	if (strcmp(line, "shutdown y") == 0) line = "poweroff";
	if (handle_uptime_cli(line, reply) || handle_tz_cli(line, reply)) return;
	if (handle_sos_cli(line, reply, silent_notifications)) {
		companion_cli_notify(requested_line, reply);
		return;
	}
#if defined(ZEPHCORE_FALL_DETECTOR)
	if (handle_fall_cli(line, reply)) return;
#endif
	if (handle_offgrid_cli(line, reply) ||
	    handle_tracking_cli(line, reply, silent_notifications) ||
	    handle_geo_cli(line, reply) ||
	    handle_findme_cli(line, reply) ||
	    handle_autoshutdown_emergency_cli(line, reply)) return;
	if (handle_vcontact_settings_cli(line, reply)) {
		return;
	}
	if (power_policy.handleCommand(line, reply, CLI_REPLY_SIZE)) {
		return;
	}
#if IS_ENABLED(CONFIG_ZEPHCORE_COMPANION_WIFI)
	if (companion_wifi_cli(line, companion_mesh.prefs, save_prefs_to_flash, reply)) {
		return;
	}
#endif
	companion_cli.setReplyHeaderUsed(reply_hdr_used);
	companion_cli.handleCommand(sender_timestamp, line, reply);
	companion_cli_notify(requested_line, reply);
}

#if ZEPHCORE_USB_STACK
/* A USB text-CLI line. The reply is framed like the repeater's serial CLI
 * ("\r\n  -> <reply>\r\n") so web serial consoles render it the same. */
static void companion_cli_run(const char *line)
{
	const char *help = companion_cli_help(line);
	if (help != nullptr) {
		zephcore_usb_companion_write_text("\r\n  -> ", 7);
		zephcore_usb_companion_write_text(help, strlen(help));
		zephcore_usb_companion_write_text("\r\n", 2);
		return;
	}

	char reply[COMPANION_CLI_PREFIX_ROOM + CLI_REPLY_SIZE];
	companion_mesh.handleCommand(line, 0, reply);
	if (reply[0] != '\0') {
		zephcore_usb_companion_write_text("\r\n  -> ", 7);
		zephcore_usb_companion_write_text(reply, strlen(reply));
	}
	zephcore_usb_companion_write_text("\r\n", 2);
}

/* USB adapter callback (sysworkq): queue the line for companion_cli_run(). */
static void companion_cli_dispatch(const char *line)
{
	struct companion_cli_line c;
	strncpy(c.buf, line, sizeof(c.buf) - 1);
	c.buf[sizeof(c.buf) - 1] = '\0';
	if (k_msgq_put(&companion_cli_queue, &c, K_NO_WAIT) == 0) {
		k_event_post(&mesh_events, MESH_EVENT_BLE_RX);
	} else {
		zephcore_usb_companion_write_text("\r\n  -> busy\r\n", 13);
	}
}

#endif  /* ZEPHCORE_USB_STACK */

#endif

/* GPS on/off (persisted by the CMD_SET_CUSTOM_VAR handler) */
static void gps_enable_callback(bool enabled)
{
	struct gps_state_info gsi;

	LOG_INF("GPS %s", enabled ? "enabled" : "disabled");
	ui_set_gps_enabled(enabled);
	gps_get_state_info(&gsi);
	ui_set_gps_state(gsi.state, gsi.last_fix_age_s, gsi.next_search_s);
	ui_request_render();
}

/* A GPS state transition for the main thread (from the system work queue). */
static void gps_event_callback(void)
{
	k_event_post(&mesh_events, MESH_EVENT_GPS_ACTION);
}

/* A validated fix (main thread, from gps_process_event()): set the clock and
 * the node position. */
static void gps_fix_callback(double lat, double lon, int64_t utc_time)
{
	if (utc_time > 0) {
		LOG_INF("GPS fix: RTC sync time=%lld", utc_time);
		rtc_clock.setCurrentTime((uint32_t)utc_time);
		time_sync_report(TIME_SYNC_GPS);
#ifdef ZEPHCORE_LORA
		/* Arm the time-sync drift envelope; activate a deferred v-contact. */
		companion_mesh.noteGPSTimeSync();
		companion_mesh.vcontactClockSynced();
#endif
	}

	sensors.node_lat = lat;
	sensors.node_lon = lon;

#ifdef ZEPHCORE_LORA
	/* RAM only, as upstream: GPS jitter would otherwise rewrite prefs on
	 * nearly every fix. It reaches flash with the next real settings save. */
	if (lat != companion_mesh.prefs.node_lat || lon != companion_mesh.prefs.node_lon) {
		companion_mesh.prefs.node_lat = lat;
		companion_mesh.prefs.node_lon = lon;
		/* Zephyr LOG has no %f by default */
		int lat_deg = (int)lat;
		int lon_deg = (int)lon;
		int lat_frac = (int)((lat - lat_deg) * 1000000);
		int lon_frac = (int)((lon - lon_deg) * 1000000);
		if (lat_frac < 0) lat_frac = -lat_frac;
		if (lon_frac < 0) lon_frac = -lon_frac;
		LOG_INF("GPS fix: position updated lat=%d.%06d lon=%d.%06d",
			lat_deg, lat_frac, lon_deg, lon_frac);
	}

	{
		struct gps_position gpos;

		gps_get_position(&gpos);
		int32_t lat_mdeg = (int32_t)(lat * 1000.0);
		int32_t lon_mdeg = (int32_t)(lon * 1000.0);

		ui_set_gps_data(true, (uint8_t)gpos.satellites,
				lat_mdeg, lon_mdeg, gpos.altitude_mm);
	}
	k_event_post(&mesh_events, MESH_EVENT_GPS_FIX);
#else
	ARG_UNUSED(lat);
	ARG_UNUSED(lon);
#endif
}

#if IS_ENABLED(CONFIG_BT)
/* bt_ready callback — BLE stack is up, start advertising */
static void bt_ready(int err)
{
	if (err) {
		LOG_ERR("bt_enable failed: %d", err);
		return;
	}

	/* Advertises only if still enabled: the ble_disabled pref was applied
	 * before bt_enable() (disableBluetooth() is recorded until start). */
#ifdef ZEPHCORE_LORA
	zephcore_ble_start(companion_mesh.getDeviceName());
#else
	zephcore_ble_start(NULL);
#endif
}
#endif /* CONFIG_BT */

int main(void)
{
	zephyr_board.clearBootloaderMagic();

	/* USB up first so the host can enumerate; wait up to 1 s for the port to
	 * open (DTR), else the banner is buffered for a later attach. */
#if ZEPHCORE_USB_STACK && DT_HAS_COMPAT_STATUS_OKAY(zephyr_cdc_acm_uart) && \
	!IS_ENABLED(CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT) && \
	(IS_ENABLED(CONFIG_USB_CDC_ACM) || IS_ENABLED(CONFIG_USBD_CDC_ACM_CLASS))
	zephcore_usbd_init();
	zephcore_usbd_wait_dtr(1000);
#endif
	LOG_INF("=== ZephCore starting ===");

	/* Restart-reason notice for the v-contact (cause captured at POST_KERNEL by
	 * helpers/boot_info.c), queued once the mesh is up. A power-integrity
	 * breadcrumb; a debugger reset or a button wake is quiet. */
	char boot_cause_msg[512];
	boot_cause_msg[0] = '\0';
	{
		const uint32_t quiet = RESET_LOW_POWER_WAKE | RESET_DEBUG;

		if ((zephcore_boot_reset_cause_labelled() & ~quiet) != 0) {
			int n = snprintf(boot_cause_msg, sizeof(boot_cause_msg), "Перезагрузка:");
			(void)zephcore_boot_reset_cause_str(boot_cause_msg + n,
							    sizeof(boot_cause_msg) - n, true);
		}
	}

	if (!ZephyrDataStore::mount()) {
		LOG_ERR("LittleFS mount failed");
	}
	data_store.begin();

	/* A low-battery shutdown with no app connected left its reason in flash
	 * (see companion_shutdown_hook); the hardware cause cannot tell. */
	{
		uint8_t sdr = zephcore_shutdown_reason();
		if (sdr == ZC_SHUTDOWN_LOW_VOLTAGE) {
			size_t l = strlen(boot_cause_msg);
			snprintf(boot_cause_msg + l, sizeof(boot_cause_msg) - l,
				 "%sПрошлое выключение: низкий заряд",
				 l ? " | " : "");
		}
	}

	/* Before bt_enable(): a first boot may have to erase the BLE bond store. */
	data_store.adoptVolume();

	sensors.begin();
#ifdef ZEPHCORE_LORA
	zephyr_board.captureBootVoltage();
#endif

	/* Wall-clock time from a battery-backed RTC, if one answers on I2C. */
	{
		uint32_t rtc_epoch;
		if (zephcore_rtc_restore(&rtc_epoch)) {
			rtc_clock.seedCurrentTime(rtc_epoch);
		}
	}

	/* Charge/heartbeat work starts inside ui_init(). */
	ui_set_battery_provider(get_battery_mv, []() { return zephyr_board.getBattPercent(); });
	ui_set_power_source_provider([]() { return zephyr_board.isExternalPowered(); });

	/* Buttons, buzzer, display; without a display UI, put any OLED to sleep. */
	ui_init();
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DESIGN_JOYSTICK)
	joystick_ui_hooks_register(&joystick_ui_task, joystick_signal_refresh, joystick_signal_tx);
#endif
#if !IS_ENABLED(CONFIG_ZEPHCORE_UI_DISPLAY)
	oled_sleep();
#endif

	if (gps_is_available()) {
		LOG_INF("GPS available");
		gps_set_enable_callback(gps_enable_callback);
		gps_set_fix_callback(gps_fix_callback);
		gps_set_event_callback(gps_event_callback);
	}

	if (env_sensors_available()) {
		LOG_INF("Environment sensors available");
	}

#ifdef ZEPHCORE_LORA
	/* Defaults from initNodePrefs() only: a field defaulted here instead is
	 * missed by its other callers, and a missing default reads back as 0 on
	 * upgrade (that zeroed probe_interval and cad_auto once). The one override
	 * lets a board's Kconfig GPS duty win over the 300 s companion figure. */
	initNodePrefs(&companion_mesh.prefs);
	companion_mesh.prefs.gps_interval = CONFIG_ZEPHCORE_GPS_POLL_INTERVAL_SEC; /* 0 = always on */

	companion_mesh.prefs.tz_offset = companion_mesh.prefs.ui_timezone_offset_minutes / 60.0f;
	data_store.loadPrefs(companion_mesh.prefs);
	ui_set_timezone_offset_minutes(tz_offset_minutes(companion_mesh.prefs.tz_offset));
	/* A repeat flag stored on a non-off-grid frequency is dropped. */
	if (mesh::offgridNormalize(companion_mesh.prefs)) {
		LOG_WRN("offgrid mode dropped: not on an off-grid frequency");
		data_store.savePrefs(companion_mesh.prefs);
	}
	/* ESP32 light sleep: `powersaving off` holds a sleep lock. No-op elsewhere. */
	zc_pm_set_powersaving(companion_mesh.prefs.powersaving_enabled != 0);
	/* Until the first fix, the position is the configured one. */
	sensors.node_lat = companion_mesh.prefs.node_lat;
	sensors.node_lon = companion_mesh.prefs.node_lon;

	/* Saved BLE PIN (0 = Kconfig default) */
#if IS_ENABLED(CONFIG_BT)
	if (companion_mesh.prefs.ble_pin >= 100000 && companion_mesh.prefs.ble_pin <= 999999) {
		zephcore_ble_set_passkey(companion_mesh.prefs.ble_pin);
		LOG_INF("BLE passkey loaded from prefs: %06u", companion_mesh.prefs.ble_pin);
	}
#endif

	/* The radio reads its preset through this from Radio::begin() on. */
	lora_radio.setPrefs(&companion_mesh.prefs);

	data_store.loadContacts(&companion_mesh);
	data_store.loadChannels(&companion_mesh);

	/* First boot: the Public channel */
	if (companion_mesh.getNumChannels() == 0) {
		// "izOH6cXN6mrJ5e26oRXNcg==" decoded
		static const uint8_t public_psk[16] = {
			0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
			0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72
		};
		companion_mesh.addChannel("Public", public_psk, 16);
		data_store.saveChannels(&companion_mesh);
		LOG_INF("Added default Public channel");
	}

	/* First boot generates the identity (ZephyrRNG::generateFirstBootIdentity). */
	mesh::LocalIdentity self_identity;
	if (!data_store.loadMainIdentity(self_identity)) {
		mesh::ZephyrRNG::generateFirstBootIdentity(self_identity);
		data_store.saveMainIdentity(self_identity);
	}
	companion_mesh.self_id = self_identity;

	/* Default node name: the first 4 bytes of the public key. */
	if (companion_mesh.prefs.node_name[0] == '\0') {
		snprintf(companion_mesh.prefs.node_name, sizeof(companion_mesh.prefs.node_name),
			 "%02X%02X%02X%02X",
			 self_identity.pub_key[0], self_identity.pub_key[1],
			 self_identity.pub_key[2], self_identity.pub_key[3]);
	}

	companion_mesh.setBatteryCallback(get_battery_mv);
	companion_mesh.setBoard(&zephyr_board);
	companion_mesh.setRadioReconfigureCallback(radio_reconfigure);
	companion_mesh.setPinChangeCallback([](uint32_t new_pin) {
#if IS_ENABLED(CONFIG_BT)
		zephcore_ble_set_passkey(new_pin);
#else
		ARG_UNUSED(new_pin);
#endif
	});
	companion_mesh.setCLICallback(companion_cli_exec);
	companion_mesh.setCLIHelpCallback(companion_app_cli_help);
	companion_mesh.setRemoteCLIHelpCallback(companion_remote_cli_help);
	companion_mesh_ptr = &companion_mesh;

	lora_radio.setRxCallback(lora_rx_callback, nullptr);
	lora_radio.setTxDoneCallback(lora_tx_done_callback, nullptr);
	companion_mesh.setTxQueuedCallback(tx_queued_callback, nullptr);

	companion_mesh.begin();

	/* After begin(), which derives the v-contact key. */
	if (boot_cause_msg[0] != '\0') {
		companion_mesh.vcontactNotify(boot_cause_msg);
	}

	/* Mounting orientation, as early as prefs allow (the splash may show
	 * upright first). A panel that cannot rotate stays native. */
	zephcore_input_set_flipped(companion_mesh.prefs.input_rotate != 0);
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DISPLAY)
	if (companion_mesh.prefs.display_rotate) {
		mc_display_set_rotated(true);
	}
#endif

	ui_set_node_name(companion_mesh.prefs.node_name);
	ui_push_radio_state(lora_radio);
	ui_set_battery(zephyr_board.getBattMilliVolts(), zephyr_board.getBattPercent());
	const bool gps_boot_enabled = gps_switch_effective_enabled(ui_gps_switch_present(),
							 ui_gps_switch_is_on(),
							 companion_mesh.prefs.gps_enabled != 0);
	ui_set_gps_available(gps_is_available());
	ui_set_gps_enabled(gps_boot_enabled);
	ui_set_ble_enabled(companion_mesh.prefs.ble_disabled != 1);  /* BLE starts advertising at boot */
#if IS_ENABLED(CONFIG_ZEPHCORE_COMPANION_WIFI)
	ui_set_wifi_enabled(companion_mesh.prefs.wifi_enabled != 0);
#endif

	ui_set_offgrid_mode(companion_mesh.isOffgridActive());
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DISPLAY) && IS_ENABLED(CONFIG_ZEPHCORE_UI_DESIGN_BUTTON)
	ui_pages_set_tracking(false, companion_mesh.prefs.tracking_interval_minutes);
#endif
	LOG_INF("offgrid mode: %s (from prefs)",
		companion_mesh.isOffgridActive() ? "on" : "off");

	/* buzzer_init() starts quiet, so the saved mode is always applied. */
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
	{
		uint8_t bmode = zephcore_buzzer_mode_from_prefs(companion_mesh.prefs.buzzer_quiet);
		zephcore_buzzer_set_mode(bmode, false);
		ui_set_buzzer_mode(bmode);
		LOG_INF("buzzer: mode=%u (from prefs)", bmode);
	}
	ui_play_startup_chime();
#endif

	/* Straight to the LED gate: it also governs the TX LED and exists in every
	 * build, UI or not. */
	bool leds_off = companion_mesh.prefs.leds_disabled != 0;
	zephcore_leds_set_disabled(leds_off);
	zephcore_leds_set_radio_mode(companion_mesh.prefs.leds_radio_mode);
	zephcore_leds_set_hb_mode(companion_mesh.prefs.leds_hb_mode);
	zephcore_led_set_brightness_pct(companion_mesh.prefs.led_brightness);
	ui_set_heartbeat_led(!leds_off);
	LOG_INF("LEDs: %s (from prefs)", leds_off ? "disabled" : "enabled");

	/* The GPS is powered at boot: match its power to prefs, then start the
	 * state machine if enabled. */
	if (gps_is_available()) {
		gps_set_poll_interval_sec(companion_mesh.prefs.gps_interval);
		gps_set_standby_max_sec(companion_mesh.prefs.gps_standby_max);
		gps_ensure_power_state(gps_boot_enabled);

		if (gps_boot_enabled) {
			LOG_INF("GPS: Restoring enabled state");
			gps_enable(true);
		}
	}

	/* Geofence survives reboot (ADR 0011): re-pin the duty like geo on and
	 * report the zone state once on the first fix. */
	if (companion_mesh.prefs.geo_on && gps_is_available()) {
		companion_geo.flags |= GEO_FLAG_ENABLED;
		if (!gps_is_enabled()) {
			gps_enable(true);
		}
		gps_set_poll_interval_sec(GEO_DUTY_SEC);
		geofence_boot_arm(&companion_geo.fsm);
		LOG_INF("geo: restored from prefs (radius %u m)",
			companion_mesh.prefs.geo_radius_m);
	}

	/* Apply RX boost and duty cycle from prefs */
	lora_radio.setRxBoost(companion_mesh.prefs.rx_boost != 0);
	lora_radio.setFemRxEnable(companion_mesh.prefs.fem_rxgain != 0);
	lora_radio.enableRxDutyCycle(companion_mesh.prefs.rx_duty_cycle != 0);
	lora_radio.setCadParams(companion_mesh.prefs.cad_auto != 0,
				companion_mesh.prefs.cad_offset,
				companion_mesh.prefs.probe_interval,
				companion_mesh.prefs.cad_busycap,
				companion_mesh.prefs.cad_base);
	/* Persist the (offset, base) in force if setCadParams() re-anchored it
	 * across a base-table change, so the next upgrade has an anchor too. */
	if (companion_mesh.prefs.cad_base != lora_radio.cadBasePeak() ||
	    companion_mesh.prefs.cad_offset != lora_radio.getCadOffset()) {
		companion_mesh.prefs.cad_offset = lora_radio.getCadOffset();
		companion_mesh.prefs.cad_base = lora_radio.cadBasePeak();
		data_store.savePrefs(companion_mesh.prefs);
	}

	/* LR2021 side detectors (multi-SF RX). A set rejected for the current
	 * SF/BW leaves them off; other radios report them unsupported. */
	{
		uint8_t n = 0;
		while (n < EXTRA_SF_MAX && companion_mesh.prefs.extra_sf[n] != 0) n++;
		if (n > 0 && !lora_radio.configSideDetectors(companion_mesh.prefs.extra_sf, n)) {
			LOG_WRN("extra.sf %u SFs rejected for current SF/BW — side detectors off", n);
		}
	}
	ui_push_radio_state(lora_radio);  /* side detectors changed the runtime state */

	/* ADC multiplier override (0 = devicetree default) */
	zephyr_board.setAdcMultiplier(companion_mesh.prefs.adc_multiplier);

	k_event_init(&mesh_events);

#if defined(ZEPHCORE_FALL_DETECTOR)
	fall_detector_set_sensitivity(companion_mesh.prefs.fall_sensitivity);
	fall_detector_set_enabled(companion_mesh.prefs.fall_enabled != 0);
	fall_detector_set_prealert_callback([]() {
		k_event_post(&mesh_events, MESH_EVENT_FALL_PREALERT);
	});
	fall_detector_begin([]() {
		k_event_post(&mesh_events, MESH_EVENT_FALL_DETECTED);
	});
#endif

	ui_mesh_actions_init(&mesh_events, MESH_EVENT_UI_ACTION,
			     &companion_mesh, &data_store,
			     &lora_radio, &zephyr_board, &rtc_clock);

#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DESIGN_JOYSTICK)
	joystick_ui_task.begin(&companion_mesh, &rtc_clock, &companion_mesh.prefs);
#endif
#endif

	/* Register every transport this build has, as upstream's main.cpp does. */
#if IS_ENABLED(CONFIG_BT)
	zephcore_ble_init(&ble_cbs);
	interface_manager.addInterface(InterfaceType::Bluetooth, &ble_interface);
#endif
#if ZEPHCORE_USB_STACK
	zephcore_usb_companion_init(&link_cbs);
	/* The text CLI starts when the first byte is not '<'. */
	zephcore_usb_companion_set_cli_line_cb(companion_cli_dispatch);
	interface_manager.addInterface(InterfaceType::USB, &usb_interface);
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_TRANSPORT_TCP)
	tcp_companion_init(&link_cbs);
	interface_manager.addInterface(InterfaceType::WiFi, &tcp_interface);
#endif

	/* Enables every interface; the ble_disabled pref then turns BLE back off
	 * before the stack starts advertising. */
#ifdef ZEPHCORE_LORA
	companion_mesh.startInterface(companion_serial);
	if (companion_mesh.prefs.ble_disabled) {
		interface_manager.disableBluetooth();
	}
#else
	companion_serial.enable();
#endif

#if IS_ENABLED(CONFIG_ZEPHCORE_TRANSPORT_TCP)
	tcp_companion_start(CONFIG_ZEPHCORE_TCP_PORT);
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_COMPANION_WIFI) && defined(ZEPHCORE_LORA)
	companion_wifi_start(companion_mesh.prefs);
#endif
#if IS_ENABLED(CONFIG_BT)
	if (bt_enable(bt_ready) != 0) {
		LOG_ERR("bt_enable failed");
	}
#endif

	/* The main thread becomes the mesh event loop (see MESH_EVENT_*). */
#ifdef ZEPHCORE_LORA
	mesh_event_loop();  /* Never returns */
#else
	for (;;) {
		k_sleep(K_FOREVER);
	}
#endif

	return 0;
}
