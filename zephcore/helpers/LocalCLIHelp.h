/*
 * SPDX-License-Identifier: MIT
 * Local serial CLI help.  These strings are deliberately emitted only by
 * local UART/USB entry points and the loopback vContact: replies travelling
 * through LoRa retain their bounded packet buffers and never expose the
 * command catalogue.
 */
#pragma once

#include <string.h>

enum class LocalCLIHelpRole {
	Companion,
	Repeater,
	RoomServer,
};

static inline const char *local_cli_help(LocalCLIHelpRole role, const char *line)
{
	if (strcmp(line, "help") != 0 && strcmp(line, "?") != 0) {
		return nullptr;
	}

	static const char common[] =
		"ver\r\nboard\r\nadvert\r\nadvert.zerohop\r\n"
		"clock [sync]\r\ntime <epoch>\r\ngps [on|off|setloc|advert]\r\n"
		"neighbors\r\nneighbor.remove <pubkey>\r\n"
		"tempradio <freq> <bw> <sf> <cr> <minutes>\r\n"
		"password <value>\r\nclear stats\r\n"
		"log [start|stop|erase]\r\nstats-packets\r\nstats-radio\r\nstats-core\r\n"
		"get/set dutycycle\r\nget/set af\r\nget/set int.thresh\r\n"
		"get/set agc.reset.interval\r\nget/set multi.acks\r\n"
		"get/set allow.read.only\r\nget/set flood.advert.interval\r\n"
		"get/set advert.interval\r\nget/set guest.password\r\n"
		"get/set prv.key\r\nget/set name\r\nget/set repeat\r\n"
		"get/set lat\r\nget/set lon\r\nget/set radio\r\n"
		"get/set radio.rxgain\r\nget/set rxdelay\r\nget/set txdelay\r\n"
		"get/set apc.margin\r\nget/set flood.max.advert\r\n"
		"get/set flood.max.unscoped\r\nget/set flood.max\r\n"
		"get/set direct.txdelay\r\nget/set backoff.multiplier\r\n"
		"get/set owner.info\r\nget/set path.hash.mode\r\n"
		"get/set loop.detect\r\nget/set tx\r\nget/set freq\r\n"
		"get/set adc.multiplier\r\nget/set rxduty\r\n"
		"get/set gps duty\r\nget/set meshtimesync\r\nget/set tz\r\n"
		"get public.key\r\nget role\r\nget bootloader.ver\r\n"
		"get dc.restarts\r\nget tx apc\r\nget cad\r\n"
		"set cad.auto\r\nset cad.offset\r\nset cad.probe.interval\r\n"
		"set cad.busycap\r\nset cad.reset\r\n"
		"sensor get <key>\r\nsensor set <key> <value>\r\n"
		"sensor list [start]\r\nstart dfu\r\nstart ota\r\n"
#if !defined(CONFIG_SOC_FAMILY_NORDIC_NRF)
		"stop ota\r\n"
#endif
		"reboot\r\nclkreboot\r\nerase\r\nhelp";
	static const char companion[] =
		"CLI companion:\r\n"
		"ver\r\nboard\r\nadvert\r\nadvert.zerohop\r\n"
		"clock [sync]\r\ntime <epoch>\r\ngps [on|off|setloc|advert]\r\n"
		"password <value>\r\nclear stats\r\n"
		"stats-packets\r\nstats-radio\r\nstats-core\r\n"
		"get/set dutycycle\r\nget/set af\r\nget/set int.thresh\r\n"
		"get/set agc.reset.interval\r\nget/set multi.acks\r\n"
		"get/set flood.advert.interval\r\nget/set advert.interval\r\n"
		"get/set prv.key\r\nget/set name\r\nget/set repeat\r\n"
		"get/set lat\r\nget/set lon\r\nget/set radio\r\n"
		"get/set radio.rxgain\r\n"
		"get/set apc.margin\r\nget/set flood.max.advert\r\n"
		"get/set flood.max.unscoped\r\nget/set flood.max\r\n"
		"get/set backoff.multiplier\r\n"
		"get/set owner.info\r\nget/set path.hash.mode\r\n"
		"get/set loop.detect\r\nget/set tx\r\nget/set freq\r\n"
		"get/set adc.multiplier\r\n"
		"get/set gps duty\r\nget/set meshtimesync\r\nget/set tz\r\n"
		"get public.key\r\nget role\r\nget bootloader.ver\r\n"
		"get dc.restarts\r\nget tx apc\r\nget cad\r\n"
		"set cad.auto\r\nset cad.offset\r\nset cad.probe.interval\r\n"
		"set cad.busycap\r\nset cad.reset\r\n"
		"get/set v.contact\r\nget/set v.batteryalert\r\n"

#if defined(CONFIG_ZEPHCORE_AUTO_SHUTDOWN_MILLIVOLTS) && \
	CONFIG_ZEPHCORE_AUTO_SHUTDOWN_MILLIVOLTS > 0
		"get/set autoshutdown\r\nget/set autoshutdown.emergency\r\n"
#endif
		"start dfu\r\nstart ota\r\n"
#if !defined(CONFIG_SOC_FAMILY_NORDIC_NRF)
		"stop ota\r\n"
#endif
		"reboot\r\nclkreboot\r\nerase\r\nhelp";
	static const char repeater[] =
		"CLI repeater:\r\n"
		"setperm <permissions> <pubkey>\r\n"
#if IS_ENABLED(CONFIG_ZEPHCORE_REPEATER_UPLINK) && IS_ENABLED(CONFIG_MQTT_LIB)
		"get/set uplink.enable\r\nget/set uplink.wifi.ssid\r\n"
		"set uplink.wifi.psk <password>\r\n"
		"get/set uplink.mqtt.host\r\nget/set uplink.mqtt.port\r\n"
		"get/set uplink.mqtt.tls\r\nget/set uplink.mqtt.user\r\n"
		"set uplink.mqtt.password <password>\r\n"
		"get/set uplink.mqtt.iata\r\nget uplink.status\r\n"
#endif
		"ver\r\nboard\r\nadvert\r\nadvert.zerohop\r\n"
		"clock [sync]\r\ntime <epoch>\r\ngps [on|off|setloc|advert]\r\n"
		"neighbors\r\nneighbor.remove <pubkey>\r\n"
		"discover.neighbors\r\n"
		"region def|get|put|remove|list|load|save\r\n"
		"region allowf|denyf|home|default\r\n"
		"tempradio <freq> <bw> <sf> <cr> <minutes>\r\n"
		"password <value>\r\nclear stats\r\n"
		"stats-packets\r\nstats-radio\r\nstats-core\r\n"
		"get/set dutycycle\r\nget/set af\r\nget/set int.thresh\r\n"
		"get/set agc.reset.interval\r\nget/set multi.acks\r\n"
		"get/set allow.read.only\r\nget/set flood.advert.interval\r\n"
		"get/set advert.interval\r\nget/set guest.password\r\n"
		"get/set prv.key\r\nget/set name\r\nget/set repeat\r\n"
		"get/set lat\r\nget/set lon\r\nget/set radio\r\n"
		"get/set radio.rxgain\r\n"
		"get/set apc.margin\r\nget/set flood.max.advert\r\n"
		"get/set flood.max.unscoped\r\nget/set flood.max\r\n"
		"get/set backoff.multiplier\r\n"
		"get/set owner.info\r\nget/set path.hash.mode\r\n"
		"get/set loop.detect\r\nget/set tx\r\nget/set freq\r\n"
		"get/set adc.multiplier\r\nget/set rxduty\r\n"
		"get/set gps duty\r\nget/set meshtimesync\r\nget/set tz\r\n"
		"get public.key\r\nget role\r\nget bootloader.ver\r\n"
		"get dc.restarts\r\nget tx apc\r\nget cad\r\n"
		"set cad.auto\r\nset cad.offset\r\nset cad.probe.interval\r\n"
		"set cad.busycap\r\nset cad.reset\r\n"
		"start dfu\r\n"
		"reboot\r\nclkreboot\r\nerase\r\nhelp";
	static const char room_server[] =
		"CLI room server:\r\n"
		"setperm <permissions> <pubkey>\r\nget acl\r\n"
		"region def|get|put|remove|list|load|save\r\n"
		"region allowf|denyf|home|default\r\n"
		"ver\r\nboard\r\nadvert\r\nadvert.zerohop\r\n"
		"clock [sync]\r\ntime <epoch>\r\n"
#if !defined(CONFIG_BOARD_RPI_PICO)
		"gps [on|off|setloc|advert]\r\n"
		"neighbors\r\nneighbor.remove <pubkey>\r\n"
#endif
		"tempradio <freq> <bw> <sf> <cr> <minutes>\r\n"
		"password <value>\r\nclear stats\r\n"
		"stats-packets\r\nstats-radio\r\nstats-core\r\n"
		"get/set dutycycle\r\nget/set af\r\nget/set int.thresh\r\n"
		"get/set agc.reset.interval\r\nget/set multi.acks\r\n"
		"get/set allow.read.only\r\nget/set flood.advert.interval\r\n"
		"get/set advert.interval\r\nget/set guest.password\r\n"
		"get/set prv.key\r\nget/set name\r\nget/set repeat\r\n"
		"get/set lat\r\nget/set lon\r\nget/set radio\r\n"
		"get/set radio.rxgain\r\n"
		"get/set apc.margin\r\nget/set flood.max.advert\r\n"
		"get/set flood.max.unscoped\r\nget/set flood.max\r\n"
		"get/set backoff.multiplier\r\n"
		"get/set owner.info\r\nget/set path.hash.mode\r\n"
		"get/set loop.detect\r\nget/set tx\r\nget/set freq\r\n"
		"get/set adc.multiplier\r\nget/set rxduty\r\n"
#if !defined(CONFIG_BOARD_RPI_PICO)
		"get/set gps duty\r\n"
#endif
		"get/set meshtimesync\r\nget/set tz\r\n"
		"get public.key\r\nget role\r\n"
		"get dc.restarts\r\nget tx apc\r\nget cad\r\n"
		"set cad.auto\r\nset cad.offset\r\nset cad.probe.interval\r\n"
		"set cad.busycap\r\nset cad.reset\r\n"
#if !defined(CONFIG_BOARD_RPI_PICO)
		"sensor get <key>\r\nsensor set <key> <value>\r\n"
		"sensor list [start]\r\n"
#endif
#if defined(CONFIG_SOC_SERIES_NRF52) || defined(CONFIG_SOC_RP2040)
		"start dfu\r\n"
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_WIFI_OTA)
		"start ota\r\nstop ota\r\n"
#endif
		"reboot\r\nclkreboot\r\nerase\r\nhelp";

	ARG_UNUSED(common);
	switch (role) {
	case LocalCLIHelpRole::Companion: return companion;
	case LocalCLIHelpRole::Repeater: return repeater;
	case LocalCLIHelpRole::RoomServer: return room_server;
	}
	return nullptr;
}
