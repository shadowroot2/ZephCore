/*
 * SPDX-License-Identifier: MIT
 * Categorized help for the repeater's remote CLI.
 */
#pragma once

#include <string.h>

/* A tagged remote reply has 158 bytes including its terminator. */
#define REPEATER_REMOTE_HELP_CAP 158

#define REMOTE_HELP_STRING(name, value) \
	static const char name[] = value; \
	static_assert(sizeof(name) <= REPEATER_REMOTE_HELP_CAP)

static inline const char *repeater_remote_cli_help(const char *line)
{
	REMOTE_HELP_STRING(groups,
		"id | rf | cad | mesh | find | region | access | gps | ui | "
		"power | sensor | stat | log | sys"
#if defined(CONFIG_ZEPHCORE_ROLE_REPEATER_BRIDGE)
		" | bridge | bridge.cfg"
#endif
#if defined(CONFIG_ZEPHCORE_REPEATER_UPLINK) && defined(CONFIG_MQTT_LIB)
		" | uplink"
#endif
		"\nhelp <group>");
	REMOTE_HELP_STRING(id,
		"get role\nget/set name\nget/set owner.info\nset prv.key\nget public.key\n"
		"password <value>\nget/set guest.password");
	REMOTE_HELP_STRING(rf,
		"get freq\nget/set: radio tx af path.hash.mode\n"
		"dutycycle int.thresh\nradio.rxgain"
#if defined(CONFIG_ZEPHCORE_RADIO_LR2021)
		" extra.sf\nget freqerr\n"
#else
		"\n"
#endif
		"get/set: radio.fem.rxgain rxduty\n"
		"tempradio f bw sf cr min"
		);
	REMOTE_HELP_STRING(cad,
		"get cad\nget cad.stats\nget cad.auto\nget cad.offset\nget probe.interval\n"
		"get cad.busycap\nget txdelay\nset cad.auto|offset|busycap|reset|probe.interval");
	REMOTE_HELP_STRING(mesh,
		"advert[.zerohop]\nget/set: advert.interval repeat loop.detect\n"
		"meshtimesync multi.acks flood.{advert.interval,max,\n"
		"max.advert,max.unscoped} backoff.multiplier");
	REMOTE_HELP_STRING(find,
		"neighbors\nneighbor.remove <pubkey>\ndiscover.neighbors");
	REMOTE_HELP_STRING(region,
		"region\nregion get|put|remove|list\nregion def|load|save\n"
		"region allowf|denyf|home|default");
	REMOTE_HELP_STRING(access,
		"setperm <permissions> <pubkey>");
	REMOTE_HELP_STRING(gps,
		"gps\ngps on|off|sync|setloc|advert\nget gps\nget gps diag\n"
		"get/set gps duty\nget/set gps standby\nget/set lat\nget/set lon");
	REMOTE_HELP_STRING(ui,
		"get/set leds\nget/set leds.brightness\nget/set leds.hb\nget/set leds.radio"
#if defined(CONFIG_ZEPHCORE_UI_DISPLAY)
		"\nget/set display.rotate\nget/set input.rotate\nget/set tz.offset"
#endif
		);
	REMOTE_HELP_STRING(power,
		"power\npowersaving on|off\nget pwrmgt.support\nget pwrmgt.source\n"
		"get pwrmgt.bootreason\nget pwrmgt.bootmv\nmore: help power.2");
	REMOTE_HELP_STRING(power2,
		"battery\nget/set adc.multiplier\nget/set battery.alert\nget/set battery.group\n"
		"get/set battery.interval\nget/set battery.threshold");
	REMOTE_HELP_STRING(sensor,
		"sensor get <key>\nsensor set <key> <value>\nsensor list <start>");
	REMOTE_HELP_STRING(stat,
		"stats-packets\nstats-radio\nstats-core\nclear stats\nget pm"
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
	REMOTE_HELP_STRING(log,
		"log start\nlog stop\nlog erase");
	REMOTE_HELP_STRING(sys,
		"ver\nboard\nuptime\nclock\nclock sync\ntime <epoch>\nget dc.restarts\n"
		"get bootloader.ver\nstart ota\nstart dfu\n"
#if defined(CONFIG_ZEPHCORE_WIFI_OTA)
		"stop ota\n"
#endif
		"reboot\nclkreboot\nshutdown y");
#if defined(CONFIG_ZEPHCORE_ROLE_REPEATER_BRIDGE)
	REMOTE_HELP_STRING(bridge,
		"bridge\nbridge on|off|ping|logs\nbridge unpair\nbridge bonds\n"
		"bridge logs N|clear\nget bridge.delay\nget bridge.mac");
	REMOTE_HELP_STRING(bridge_cfg,
		"set bridge.key <32-hex>\nget/set bridge.peer\nget/set bridge.priority\n"
		"get/set bridge.type");
#endif
#if defined(CONFIG_ZEPHCORE_REPEATER_UPLINK) && defined(CONFIG_MQTT_LIB)
	REMOTE_HELP_STRING(uplink,
		"get uplink.status\nget/set uplink.enable\nget/set uplink.mqtt.host|port|tls|user|iata\n"
		"set uplink.mqtt.password\nget/set uplink.wifi.ssid\nset uplink.wifi.psk");
#endif

	if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) return groups;
	if (strcmp(line, "help id") == 0) return id;
	if (strcmp(line, "help rf") == 0) return rf;
	if (strcmp(line, "help cad") == 0) return cad;
	if (strcmp(line, "help mesh") == 0) return mesh;
	if (strcmp(line, "help find") == 0) return find;
	if (strcmp(line, "help region") == 0) return region;
	if (strcmp(line, "help access") == 0) return access;
	if (strcmp(line, "help gps") == 0) return gps;
	if (strcmp(line, "help ui") == 0) return ui;
	if (strcmp(line, "help power") == 0) return power;
	if (strcmp(line, "help power.2") == 0) return power2;
	if (strcmp(line, "help sensor") == 0) return sensor;
	if (strcmp(line, "help stat") == 0) return stat;
	if (strcmp(line, "help log") == 0) return log;
	if (strcmp(line, "help sys") == 0) return sys;
#if defined(CONFIG_ZEPHCORE_ROLE_REPEATER_BRIDGE)
	if (strcmp(line, "help bridge") == 0) return bridge;
	if (strcmp(line, "help bridge.cfg") == 0) return bridge_cfg;
#endif
#if defined(CONFIG_ZEPHCORE_REPEATER_UPLINK) && defined(CONFIG_MQTT_LIB)
	if (strcmp(line, "help uplink") == 0) return uplink;
#endif
	return nullptr;
}

#undef REMOTE_HELP_STRING
#undef REPEATER_REMOTE_HELP_CAP
