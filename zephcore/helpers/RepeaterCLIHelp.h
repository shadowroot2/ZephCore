/*
 * SPDX-License-Identifier: MIT
 * Categorized help for the repeater's remote CLI.
 */
#pragma once

#include <string.h>
#if defined(__ZEPHYR__)
#include <zephyr/devicetree.h>
#include <helpers/pm_sleep_guard.h>
#if defined(CONFIG_ZEPHCORE_UI_DISPLAY)
#include <helpers/ui/display.h>
#define REPEATER_HELP_HAS_DISPLAY_ROTATE MC_DISPLAY_ROTATE_SUPPORTED
#else
#define REPEATER_HELP_HAS_DISPLAY_ROTATE 0
#endif
#define REPEATER_HELP_HAS_GNSS (DT_HAS_COMPAT_STATUS_OKAY(gnss_nmea_generic) || \
	DT_HAS_COMPAT_STATUS_OKAY(quectel_lc76g) || \
	DT_HAS_COMPAT_STATUS_OKAY(luatos_air530z))
#define REPEATER_HELP_HAS_ADC (DT_NODE_EXISTS(DT_PATH(zephyr_user)) && \
	DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels))
#define REPEATER_HELP_HAS_FEM DT_NODE_HAS_PROP(DT_ALIAS(lora0), lna_bypass_gpios)
#define REPEATER_HELP_HAS_PM ZC_PM_LIGHT_SLEEP
#define REPEATER_HELP_HAS_RADIO_LED (DT_NODE_EXISTS(DT_ALIAS(lora_tx_led)) || \
	DT_NODE_EXISTS(DT_ALIAS(lora_tx_pwm_led)))
#define REPEATER_HELP_HAS_HB_LED (DT_NODE_HAS_PROP(DT_ALIAS(led0), gpios) || \
	DT_NODE_HAS_PROP(DT_ALIAS(led1), gpios))
#else
#define REPEATER_HELP_HAS_GNSS 1
#define REPEATER_HELP_HAS_ADC 1
#define REPEATER_HELP_HAS_FEM 1
#define REPEATER_HELP_HAS_PM 1
#define REPEATER_HELP_HAS_RADIO_LED 1
#define REPEATER_HELP_HAS_HB_LED 1
#define REPEATER_HELP_HAS_DISPLAY_ROTATE 1
#endif
#define REPEATER_HELP_HAS_LEDS (REPEATER_HELP_HAS_RADIO_LED || REPEATER_HELP_HAS_HB_LED)
#if defined(CONFIG_ZEPHCORE_UI_DISPLAY)
#define REPEATER_HELP_HAS_DISPLAY 1
#else
#define REPEATER_HELP_HAS_DISPLAY 0
#endif
#define REPEATER_HELP_HAS_UI (REPEATER_HELP_HAS_LEDS || REPEATER_HELP_HAS_DISPLAY)
#if defined(CONFIG_ZEPHCORE_RADIO_SX127X)
#define REPEATER_HELP_HAS_CAD 0
#else
#define REPEATER_HELP_HAS_CAD 1
#endif

/* A tagged remote reply has 158 bytes including its terminator. */
#define REPEATER_REMOTE_HELP_CAP 158

#define REMOTE_HELP_STRING(name, value) \
	static const char name[] = value; \
	static_assert(sizeof(name) <= REPEATER_REMOTE_HELP_CAP)

static inline bool repeater_cli_ota_command(const char *line)
{
	return strncmp(line, "start ota", 9) == 0 || strncmp(line, "stop ota", 8) == 0;
}

static inline const char *repeater_remote_cli_help(const char *line, bool bridge_ble = true)
{
	REMOTE_HELP_STRING(groups,
		"id | access | rf | region"
#if REPEATER_HELP_HAS_CAD
		" | cad"
#endif
		" | mesh | find | gps"
#if REPEATER_HELP_HAS_UI
		" | ui"
#endif
		" | "
		"power | batt"
#if REPEATER_HELP_HAS_GNSS
		" | sensor"
#endif
#if defined(CONFIG_ZEPHCORE_ROLE_REPEATER_BRIDGE)
		" | bridge"
#endif
#if defined(CONFIG_ZEPHCORE_REPEATER_UPLINK) && defined(CONFIG_MQTT_LIB)
		" | uplink"
#endif
		" | stat"
#if defined(CONFIG_LOG)
		" | log"
#endif
		" | sys\nhelp <group>");
	REMOTE_HELP_STRING(id,
		"get role\nget/set name\nget/set owner.info\nget public.key\nset prv.key");
	REMOTE_HELP_STRING(rf,
		"get freq\n"
#if defined(CONFIG_ZEPHCORE_RADIO_LR2021)
		"get freqerr\n"
#endif
		"get/set:\nradio\ntx\naf\npath.hash.mode\ndutycycle\nint.thresh\n"
#if !defined(CONFIG_ZEPHCORE_RADIO_SX127X)
		"radio.rxgain\n"
#endif
#if defined(CONFIG_ZEPHCORE_RADIO_LR2021)
		"extra.sf\n"
#endif
#if REPEATER_HELP_HAS_FEM
		"radio.fem.rxgain\n"
#endif
#if REPEATER_HELP_HAS_CAD
		"rxduty\n"
#endif
		"tempradio f bw sf cr min"
		);
#if REPEATER_HELP_HAS_CAD
	REMOTE_HELP_STRING(cad,
		"get cad\nget cad.stats\nget txdelay\nset cad.reset\n"
		"get cad.auto\nget cad.offset\nget probe.interval\nget cad.busycap\n"
		"set cad.auto|offset|busycap|probe.interval");
#endif
	REMOTE_HELP_STRING(mesh,
		"advert[.zerohop]\nget/set:\nrepeat\nadvert.interval\nloop.detect\n"
		"meshtimesync\nmulti.acks\n"
		"flood.{advert.interval,max,max.advert,max.unscoped}\nbackoff.multiplier");
	REMOTE_HELP_STRING(find,
		"discover.neighbors\nneighbors\nneighbor.remove <pubkey>");
	REMOTE_HELP_STRING(region,
		"region\nregion get|put|remove|list\nregion def|load|save\n"
		"region allowf|denyf|home|default");
	REMOTE_HELP_STRING(access,
		"password <value>\nget/set guest.password\n"
		"setperm <permissions> <pubkey>");
	REMOTE_HELP_STRING(gps,
#if REPEATER_HELP_HAS_GNSS
		"gps\ngps on|off\ngps sync|setloc|advert\nget gps diag\n"
		"get/set gps duty\nget/set gps standby\n"
#else
		"gps advert\n"
#endif
		"get/set lat\nget/set lon");
#if REPEATER_HELP_HAS_UI
	REMOTE_HELP_STRING(ui,
#if REPEATER_HELP_HAS_LEDS
		"get/set leds\nget/set leds.brightness"
#endif
#if REPEATER_HELP_HAS_HB_LED
		"\nget/set leds.hb"
#endif
#if REPEATER_HELP_HAS_RADIO_LED
		"\nget/set leds.radio"
#endif
#if REPEATER_HELP_HAS_DISPLAY
#if REPEATER_HELP_HAS_LEDS
		"\n"
#endif
#if REPEATER_HELP_HAS_DISPLAY_ROTATE
		"get/set display.rotate\n"
#endif
		"get/set input.rotate\nget/set tz.offset"
#endif
		);
#endif
	REMOTE_HELP_STRING(power,
		"power\n"
#if defined(CONFIG_ZEPHCORE_CLI_POWERSAVING)
		"powersaving on|off\n"
#endif
		"get pwrmgt.support\n"
#if defined(CONFIG_ZEPHCORE_CLI_PWRMGT)
		"get pwrmgt.source\n"
#endif
		"get pwrmgt.bootreason"
#if defined(CONFIG_ZEPHCORE_CLI_PWRMGT)
		"\nget pwrmgt.bootmv"
#endif
		);
	REMOTE_HELP_STRING(batt,
		"battery\n"
#if REPEATER_HELP_HAS_ADC
		"get/set adc.multiplier\n"
#endif
		"get/set battery.alert\nget/set battery.group\n"
		"get/set battery.interval\nget/set battery.threshold");
#if REPEATER_HELP_HAS_GNSS
	REMOTE_HELP_STRING(sensor,
		"sensor get <key>\nsensor set <key> <value>\nsensor list <start>");
#endif
	REMOTE_HELP_STRING(stat,
		"stats-packets\nstats-radio\nstats-core\nclear stats"
#if REPEATER_HELP_HAS_PM
		"\nget pm"
#endif
#if defined(CONFIG_ZEPHCORE_MEM_STATS)
		"\nget mem [<n>]"
#endif
#if defined(CONFIG_ZEPHCORE_POWER_DIAG)
		"\nget pwr [<n>]"
#endif
#if defined(CONFIG_ZEPHCORE_LOOP_WAKE_STATS)
		"\nget loop.wakes"
#endif
		);
#if defined(CONFIG_LOG)
	REMOTE_HELP_STRING(log,
		"log start\nlog stop");
#endif
	REMOTE_HELP_STRING(sys,
		"ver\nboard\nuptime\nclock\nclock sync\ntime <epoch>\n"
#if REPEATER_HELP_HAS_CAD
		"get dc.restarts\n"
#endif
		"get bootloader.ver\n"
#if defined(CONFIG_ZEPHCORE_CLI_DFU)
		"start dfu\n"
#endif
		"reboot"
#if defined(CONFIG_POWEROFF)
		"\nshutdown y"
#endif
		);
#if defined(CONFIG_ZEPHCORE_ROLE_REPEATER_BRIDGE)
	REMOTE_HELP_STRING(bridge_ble_help,
		"bridge [on|off]\nbridge ping|unpair|bonds\nbridge logs [N|clear]\n"
		"get bridge.delay|mac\nset bridge.key <32-hex>\n"
		"get/set:\nbridge.peer\nbridge.priority\nbridge.type");
	REMOTE_HELP_STRING(bridge_espnow_help,
		"bridge [on|off]\nbridge ping\nget bridge.delay\nget bridge.mac\n"
		"set bridge.key <32-hex>\nget/set:\nbridge.peer\nbridge.priority\nbridge.type");
#endif
#if defined(CONFIG_ZEPHCORE_REPEATER_UPLINK) && defined(CONFIG_MQTT_LIB)
	REMOTE_HELP_STRING(uplink,
		"get uplink.status\nget/set uplink.enable\nset uplink.wifi.psk\n"
		"get/set uplink.mqtt.host|port|tls|user|iata\n"
		"set uplink.mqtt.password\nget/set uplink.wifi.ssid");
#endif

	if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) return groups;
	if (strcmp(line, "help id") == 0) return id;
	if (strcmp(line, "help access") == 0) return access;
	if (strcmp(line, "help rf") == 0) return rf;
	if (strcmp(line, "help region") == 0) return region;
#if REPEATER_HELP_HAS_CAD
	if (strcmp(line, "help cad") == 0) return cad;
#endif
	if (strcmp(line, "help mesh") == 0) return mesh;
	if (strcmp(line, "help find") == 0) return find;
	if (strcmp(line, "help gps") == 0) return gps;
#if REPEATER_HELP_HAS_UI
	if (strcmp(line, "help ui") == 0) return ui;
#endif
	if (strcmp(line, "help power") == 0) return power;
	if (strcmp(line, "help batt") == 0) return batt;
#if REPEATER_HELP_HAS_GNSS
	if (strcmp(line, "help sensor") == 0) return sensor;
#endif
	if (strcmp(line, "help stat") == 0) return stat;
#if defined(CONFIG_LOG)
	if (strcmp(line, "help log") == 0) return log;
#endif
	if (strcmp(line, "help sys") == 0) return sys;
#if defined(CONFIG_ZEPHCORE_ROLE_REPEATER_BRIDGE)
	if (strcmp(line, "help bridge") == 0) return bridge_ble ? bridge_ble_help : bridge_espnow_help;
#else
	(void)bridge_ble;
#endif
#if defined(CONFIG_ZEPHCORE_REPEATER_UPLINK) && defined(CONFIG_MQTT_LIB)
	if (strcmp(line, "help uplink") == 0) return uplink;
#endif
	return nullptr;
}

#undef REMOTE_HELP_STRING
#undef REPEATER_REMOTE_HELP_CAP
#undef REPEATER_HELP_HAS_GNSS
#undef REPEATER_HELP_HAS_ADC
#undef REPEATER_HELP_HAS_FEM
#undef REPEATER_HELP_HAS_PM
#undef REPEATER_HELP_HAS_RADIO_LED
#undef REPEATER_HELP_HAS_HB_LED
#undef REPEATER_HELP_HAS_LEDS
#undef REPEATER_HELP_HAS_DISPLAY
#undef REPEATER_HELP_HAS_UI
#undef REPEATER_HELP_HAS_DISPLAY_ROTATE
#undef REPEATER_HELP_HAS_CAD
