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
		"Groups: id | rf | radio | cad | mesh | flood | region | access | gps | ui | "
		"power | battery | sensor | log | stat | sys"
#if defined(CONFIG_ZEPHCORE_ROLE_REPEATER_BRIDGE)
		" | bridge | bridge.cfg"
#endif
#if defined(CONFIG_ZEPHCORE_REPEATER_UPLINK) && defined(CONFIG_MQTT_LIB)
		" | uplink"
#endif
		"\nUse: help <group>");
	REMOTE_HELP_STRING(id,
		"get role\nget/set name\nget/set owner.info\nset prv.key\nget public.key");
	REMOTE_HELP_STRING(rf,
		"get freq\nget/set radio\nget/set tx\nget/set af\nget/set path.hash.mode\n"
		"get/set dutycycle\nget/set int.thresh");
	REMOTE_HELP_STRING(radio,
		"get/set radio.rxgain\n"
#if defined(CONFIG_ZEPHCORE_RADIO_LR2021)
		"get/set extra.sf\nget freqerr\n"
#endif
		"get/set rxduty\ntempradio <freq> <bw> <sf> <cr> <minutes>"
		"\nget/set radio.fem.rxgain"
		);
	REMOTE_HELP_STRING(cad,
		"get cad\nget cad.stats\nget cad.auto\nget cad.offset\nget probe.interval\n"
		"get cad.busycap\nset cad.auto|offset|busycap|reset|probe.interval");
	REMOTE_HELP_STRING(mesh,
		"advert\nadvert.zerohop\nget/set advert.interval\nget/set repeat\n"
		"get/set loop.detect\nget/set meshtimesync\nget/set multi.acks");
	REMOTE_HELP_STRING(flood,
		"get/set flood.advert.interval\nget/set flood.max\nget/set flood.max.advert\n"
		"get/set flood.max.unscoped\nget/set backoff.multiplier");
	REMOTE_HELP_STRING(region,
		"region\nregion get|put|remove|list\nregion def|load|save\n"
		"region allowf|denyf|home|default");
	REMOTE_HELP_STRING(access,
		"password <value>\nget/set guest.password\nsetperm <permissions> <pubkey>\n"
		"neighbors\nneighbor.remove <pubkey>\ndiscover.neighbors");
	REMOTE_HELP_STRING(gps,
		"gps\ngps on|off|sync|setloc|advert\nget gps\nget gps diag\n"
		"get/set gps duty\nget/set gps standby\nget/set lat\nget/set lon");
	REMOTE_HELP_STRING(ui,
		"leds on|off\nbuzz on|off\nget/set leds.hb\nget/set leds.radio"
		"\nget/set leds\nget/set leds.brightness"
#if defined(CONFIG_ZEPHCORE_UI_DISPLAY)
		"\nget/set display.rotate\nget/set input.rotate\nget/set tz.offset"
#endif
		);
	REMOTE_HELP_STRING(power,
		"power\npowersaving on|off\nget pwrmgt.support\nget pwrmgt.source\n"
		"get pwrmgt.bootreason\nget pwrmgt.bootmv\nshutdown y");
	REMOTE_HELP_STRING(battery,
		"battery\nget/set adc.multiplier\nget/set battery.alert\nget/set battery.group\n"
		"get/set battery.interval\nget/set battery.threshold");
	REMOTE_HELP_STRING(sensor,
		"sensor get <key>\nsensor set <key> <value>\nsensor list <start>");
	REMOTE_HELP_STRING(log,
		"log start\nlog stop\nlog erase");
	REMOTE_HELP_STRING(stat,
		"stats-packets\nstats-radio\nstats-core\nclear stats\nget pm\nget mem [<n>]\n"
		"get loop.wakes");
	REMOTE_HELP_STRING(sys,
		"ver\nboard\nuptime\nclock\nclock sync\ntime <epoch>\nget dc.restarts\n"
		"get bootloader.ver\nstart ota\nstart dfu\nreboot\nclkreboot");
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
	if (strcmp(line, "help radio") == 0) return radio;
	if (strcmp(line, "help cad") == 0) return cad;
	if (strcmp(line, "help mesh") == 0) return mesh;
	if (strcmp(line, "help flood") == 0) return flood;
	if (strcmp(line, "help region") == 0) return region;
	if (strcmp(line, "help access") == 0) return access;
	if (strcmp(line, "help gps") == 0) return gps;
	if (strcmp(line, "help ui") == 0) return ui;
	if (strcmp(line, "help power") == 0) return power;
	if (strcmp(line, "help battery") == 0) return battery;
	if (strcmp(line, "help sensor") == 0) return sensor;
	if (strcmp(line, "help log") == 0) return log;
	if (strcmp(line, "help stat") == 0) return stat;
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
