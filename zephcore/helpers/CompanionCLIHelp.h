/* SPDX-License-Identifier: MIT */
#pragma once

#include <string.h>
#include <zephyr/sys/util.h>

#define COMPANION_WIFI_HELP \
	"get wifi.status\nget wifi.ip\nget wifi.ssid\nset wifi.ssid <name>\n" \
	"get wifi.pwd\nset wifi.pwd <password>\nget wifi.enabled\n" \
	"set wifi.enabled 0|1\nset wifi.clear"

#define COMPANION_REMOTE_WIFI_HELP \
	"get wifi.status\nget wifi.ip\nget wifi.enabled\n" \
	"set wifi.enabled 0|1"

static_assert(sizeof(COMPANION_WIFI_HELP) - 1 <= 172,
	      "WiFi help must fit app CLI frame");
static_assert(sizeof(COMPANION_REMOTE_WIFI_HELP) <= 158,
	      "remote WiFi help must fit prefixed remote CLI");

static inline const char *companion_app_cli_help(const char *line)
{
	if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
		return "Groups: id | rf | cad | mesh | gps"
#if IS_ENABLED(CONFIG_ZEPHCORE_COMPANION_WIFI)
		       " | wifi"
#endif
		       " | extra | ui | power | stat | sys\n"
		       "Use: help <group>";
	}
	if (strcmp(line, "help id") == 0) {
		return "get role\nget/set name\nget/set owner.info\nget/set prv.key\nget public.key";
	}
	if (strcmp(line, "help stat") == 0) {
		return "stats-packets\nstats-radio\nstats-core\nclear stats";
	}
	if (strcmp(line, "help sys") == 0) {
		return "ver\nboard\nuptime\ntz\nclock sync\ntime <epoch>\nget dc.restarts\n"
		       "get bootloader.ver\nget/set tz.offset\nstart ota\nstart dfu\n"
		       "reboot\nshutdown y\nerase";
	}
	if (strcmp(line, "help rf") == 0) {
		return "get/set freq\nget/set radio\nget/set tx\nget/set af\n"
		       "get/set path.hash.mode\nget/set dutycycle\nget/set extra.sf\nget/set int.thresh\n"
		       "get/set radio.rxgain\nget/set radio.fem.rxgain";
	}
	if (strcmp(line, "help cad") == 0) {
		return "get cad\nset cad.auto\nset cad.offset\nset probe.interval\n"
		       "set cad.busycap\nset cad.reset";
	}
	if (strcmp(line, "help mesh") == 0) {
		return "advert\nadvert.zerohop\nget/set advert.interval\n"
		       "get/set meshtimesync\nget/set flood.advert.interval\nget/set flood.max\n"
		       "get/set flood.max.advert\nget/set flood.max.unscoped";
	}
	if (strcmp(line, "help gps") == 0) {
		return "gps on|off|setloc|advert\nget/set gps duty\nget/set lat\nget/set lon";
	}
#if IS_ENABLED(CONFIG_ZEPHCORE_COMPANION_WIFI)
	if (strcmp(line, "help wifi") == 0) {
		return COMPANION_WIFI_HELP;
	}
#endif
	if (strcmp(line, "help extra") == 0) {
		return "findme\nsos\ntracking on|off\nget/set tracking.group\n"
		       "get/set tracking.interval\n"
#if defined(ZEPHCORE_FALL_DETECTOR)
		       "fall on|off\nget/set fall.sens (1-5)\n"
#endif
		       "get/set v.contact\noffgrid on|off";
	}
	if (strcmp(line, "help ui") == 0) {
		return "buzz on|off\nleds on|off\n"
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DISPLAY)
		       "get/set display.rotate\nget/set input.rotate\n"
#endif
		       "get/set leds.hb\n"
		       "get/set leds.radio";
	}
	if (strcmp(line, "help power") == 0) {
		return "get/set adc.multiplier\nget/set autoshutdown\n"
		       "get/set autoshutdown.emergency\nget/set v.batteryalert";
	}
	return nullptr;
}

static inline const char *companion_remote_cli_help(const char *line)
{
	if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
		return "Groups: id | rf | cad | mesh | gps"
#if IS_ENABLED(CONFIG_ZEPHCORE_COMPANION_WIFI)
		       " | wifi"
#endif
		       " | extra | ui | power | stat | sys\nUse: help <group>";
	}
	if (strcmp(line, "help id") == 0) {
		return "get role\nget/set name\nget/set owner.info\nget public.key";
	}
	if (strcmp(line, "help rf") == 0) {
		return "get: freq radio tx\nget/set: af path.hash.mode dutycycle\n"
		       "extra.sf int.thresh radio.rxgain radio.fem.rxgain";
	}
	if (strcmp(line, "help cad") == 0) {
		return "get cad\nset cad.auto\nset cad.offset\nset probe.interval\n"
		       "set cad.busycap\nset cad.reset";
	}
	if (strcmp(line, "help mesh") == 0) {
		return "advert[.zerohop]\nget/set: advert.interval meshtimesync\n"
		       "flood.{advert.interval,max,max.advert,max.unscoped}";
	}
	if (strcmp(line, "help gps") == 0) {
		return "gps on|off|setloc|advert\nget/set gps duty\nget/set lat\nget/set lon";
	}
#if IS_ENABLED(CONFIG_ZEPHCORE_COMPANION_WIFI)
	if (strcmp(line, "help wifi") == 0) return COMPANION_REMOTE_WIFI_HELP;
#endif
	if (strcmp(line, "help extra") == 0) {
		return "findme\nsos\ntracking on|off\nget/set tracking.group\n"
		       "get/set tracking.interval\n"
#if defined(ZEPHCORE_FALL_DETECTOR)
		       "fall on|off\nget/set fall.sens (1-5)\n"
#endif
		       "get/set v.contact";
	}
	if (strcmp(line, "help ui") == 0) {
		return "buzz on|off\nleds on|off\n"
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_DISPLAY)
		       "get/set display.rotate\nget/set input.rotate\n"
#endif
		       "get/set leds.hb\nget/set leds.radio";
	}
	if (strcmp(line, "help power") == 0) {
		return "get adc.multiplier\nget autoshutdown\n"
		       "get/set autoshutdown.emergency\nget/set v.batteryalert";
	}
	if (strcmp(line, "help stat") == 0) {
		return "stats-packets\nstats-radio\nstats-core";
	}
	if (strcmp(line, "help sys") == 0) {
		return "ver\nboard\nuptime\ntz\nclock sync\ntime <epoch>\nget dc.restarts\n"
		       "get bootloader.ver\nget/set tz.offset\nreboot";
	}
	return nullptr;
}

static inline bool companion_remote_cli_command_allowed(const char *cmd)
{
	static const char *const exact[] = {
		"get radio", "get freq", "get af", "get dutycycle", "get int.thresh", "get cad",
		"get radio.rxgain", "get tx", "get rxdelay", "get agc.reset.interval",
		"get path.hash.mode", "get txdelay", "get direct.txdelay", "get extra.sf",
		"get radio.fem.rxgain", "reboot", "get name",
		"get wifi.enabled", "get wifi.status", "get wifi.ip", "board", "ver",
		"get tz.offset", "get role", "get public.key", "uptime", "tz", "clock sync",
		"advert", "advert.zerohop", "findme", "sos", "tracking on", "tracking off",
		"fall on", "fall off", "leds on", "leds off", "buzz on", "buzz off",
		"stats-packets", "stats-radio", "stats-core", "gps on", "gps off",
		"gps setloc", "gps advert",
		"get dc.restarts", "get bootloader.ver", "get owner.info", "get advert.interval",
		"get meshtimesync", "get flood.advert.interval", "get flood.max",
		"get flood.max.advert", "get flood.max.unscoped", "get gps duty", "get lat",
		"get lon", "get tracking.group", "get tracking.interval", "get fall.sens",
		"get v.contact", "get leds.hb", "get leds.radio", "get adc.multiplier",
		"get autoshutdown", "get autoshutdown.emergency", "get v.batteryalert",
		"get display.rotate", "get input.rotate",
	};
	static const char *const prefix[] = {
		"set af ", "set dutycycle ", "set int.thresh ", "set cad ",
		"set radio.rxgain ", "get tx ", "set rxdelay ", "set agc.reset.interval ",
		"set path.hash.mode ", "set txdelay ", "set direct.txdelay ", "set extra.sf ",
		"set radio.fem.rxgain ", "set name ", "set wifi.enabled ", "set tz.offset ",
		"time ", "set owner.info ", "set advert.interval ", "set meshtimesync ",
		"set flood.advert.interval ", "set flood.max ", "set flood.max.advert ",
		"set flood.max.unscoped ", "set cad.auto ", "set cad.offset ",
		"set probe.interval ", "set cad.busycap ", "set cad.reset", "gps advert ",
		"set gps duty ", "set lat ", "set lon ", "set tracking.group ",
		"set tracking.interval ", "set fall.sens ", "set v.contact ", "set leds.hb ",
		"set leds.radio ", "set autoshutdown.emergency ", "set v.batteryalert ",
		"set display.rotate ", "set input.rotate ",
	};
	for (const char *e : exact) {
		if (strcmp(cmd, e) == 0) return true;
	}
	for (const char *p : prefix) {
		if (strncmp(cmd, p, strlen(p)) == 0) return true;
	}
	return false;
}
