/*
 * SPDX-License-Identifier: MIT
 * Local serial CLI help.  These strings are deliberately emitted only by
 * local UART/USB entry points and the loopback vContact: replies travelling
 * through LoRa retain their bounded packet buffers and never expose the
 * command catalogue.
 */
#pragma once

#include <string.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>
#include <helpers/BatteryAlertDefaults.h>

enum class LocalCLIHelpRole {
	Companion,
	Repeater,
	RoomServer,
};

/* A complete repeater help must fit in the asynchronous serial TX queue. */
static constexpr size_t REPEATER_CLI_TX_BUF_SIZE = 4096;

#define CLI_UI_RADIO_HELP \
	"get/set leds.radio\r\nget/set leds.hb\r\n" \
	"get/set display.rotate\r\nget/set input.rotate\r\n" \
	"get/set tz.offset (hours)\r\n" \
	"get/set radio.fem.rxgain\r\nget/set extra.sf\r\n"

static inline const char *local_cli_help(LocalCLIHelpRole role, const char *line)
{
	if (strcmp(line, "help") != 0 && strcmp(line, "?") != 0) {
		return nullptr;
	}

	static const char companion[] =
		"CLI companion:\r\n"
		"ver\r\nboard\r\nuptime\r\nadvert\r\nadvert.zerohop\r\n"
		"clock sync\r\ntime <epoch>\r\n"
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
		"findme\r\n"
#endif
		"sos\r\n"
		"gps on|off|setloc|advert\r\n"
		"tracking on|off\r\n"
#if defined(CONFIG_BOARD_THINKNODE_M3) || defined(CONFIG_BOARD_T1000_E)
		"fall on|off\r\n"
#endif
#if DT_NODE_HAS_PROP(DT_ALIAS(led0), gpios) || DT_NODE_HAS_PROP(DT_ALIAS(led1), gpios)
		"leds on|off\r\n"
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
		"buzz on|off\r\n"
#endif
		"offgrid on|off\r\n"
		"password <value>\r\n"
		"stats-packets\r\nstats-radio\r\nstats-core\r\nclear stats\r\n"
		"get role\r\nget public.key\r\nget gps diag\r\n"
		"get dc.restarts\r\nget tx apc\r\nget cad\r\nget bootloader.ver\r\n"
		"set cad.auto\r\nset cad.offset\r\nset probe.interval\r\n"
		"set cad.busycap\r\nset cad.reset\r\n"
		"get/set adc.multiplier\r\nget/set advert.interval\r\nget/set af\r\n"
#if defined(CONFIG_ZEPHCORE_AUTO_SHUTDOWN_MILLIVOLTS) && \
	CONFIG_ZEPHCORE_AUTO_SHUTDOWN_MILLIVOLTS > 0
		"get/set autoshutdown\r\nget/set autoshutdown.emergency\r\n"
#endif

#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
		"get/set buzzer off|on|vibrate|sound\r\n"
#endif
		"get/set display.rotate\r\nget/set dutycycle\r\nget/set extra.sf\r\n"
#if defined(CONFIG_BOARD_THINKNODE_M3) || defined(CONFIG_BOARD_T1000_E)
		"get/set fall.sens (1-5)\r\n"
#endif
		"get/set flood.advert.interval\r\nget/set flood.max\r\n"
		"get/set flood.max.advert\r\nget/set flood.max.unscoped\r\n"
		"get/set freq\r\nget/set gps duty\r\nget/set input.rotate\r\n"
		"get/set int.thresh\r\nget/set lat\r\nget/set leds.hb\r\n"
		"get/set leds.radio\r\nget/set lon\r\nget/set meshtimesync\r\n"
		"get/set multi.acks\r\nget/set name\r\n"
#if defined(CONFIG_BOARD_HELTEC_WIFI_LORA32_V43)
		"get/set output.power 22|28|default (nominal dBm)\r\n"
#endif
		"get/set owner.info\r\n"
		"get/set path.hash.mode\r\nget/set prv.key\r\nget/set radio\r\n"
		"get/set radio.fem.rxgain\r\nget/set radio.rxgain\r\n"
		"get/set tracking.group\r\nget/set tracking.interval\r\n"
		"get/set tx\r\nget/set tz.offset (hours)\r\n"
		"get/set v.batteryalert\r\nget/set v.contact\r\n"
		"start ota\r\nstart dfu\r\n"
#if !defined(CONFIG_SOC_FAMILY_NORDIC_NRF)
		"stop ota\r\n"
#endif
		"reboot\r\nclkreboot\r\n"
#if IS_ENABLED(CONFIG_POWEROFF)
		"shutdown y\r\n"
#endif
		"erase\r\nhelp";
	static const char repeater[] =
#if IS_ENABLED(CONFIG_ZEPHCORE_ROLE_REPEATER_BRIDGE)
		"CLI repeater-bridge:\r\n"
#else
		"CLI repeater:\r\n"
#endif
		"ver\r\n"
		"board\r\n"
		"battery (mV, %, uptime, alert settings)\r\n"
		"uptime\r\n"
		"advert\r\n"
		"advert.zerohop\r\n"
		"clock sync\r\n"
		"time <epoch>\r\n"
#if DT_HAS_COMPAT_STATUS_OKAY(gnss_nmea_generic) || \
    DT_HAS_COMPAT_STATUS_OKAY(u_blox_m8) || \
    DT_HAS_COMPAT_STATUS_OKAY(u_blox_f9p) || \
    DT_HAS_COMPAT_STATUS_OKAY(quectel_lcx6g) || \
    DT_HAS_COMPAT_STATUS_OKAY(quectel_lc76g) || \
    DT_HAS_COMPAT_STATUS_OKAY(luatos_air530z)
		"gps on|off|setloc|advert\r\n"
#endif
#if DT_NODE_HAS_PROP(DT_ALIAS(led0), gpios) || DT_NODE_HAS_PROP(DT_ALIAS(led1), gpios)
		"leds on|off\r\n"
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_UI_BUZZER)
		"buzz on|off\r\n"
#endif
		"password <value>\r\n"
#if IS_ENABLED(CONFIG_ZEPHCORE_ROLE_REPEATER_BRIDGE)
		"bridge on|off|ping|keygen|unpair (keygen: local)\r\n"
#endif
		"neighbors\r\n"
		"neighbor.remove <pubkey>\r\n"
		"discover.neighbors\r\n"
		"region def|get|put|remove|list|load|save\r\n"
		"region allowf|denyf|home|default\r\n"
		"tempradio <freq> <bw> <sf> <cr> <minutes>\r\n"
		"setperm <permissions> <pubkey>\r\n"
		"stats-packets\r\n"
		"stats-radio\r\n"
		"stats-core\r\n"
		"clear stats\r\n"
		"get role\r\n"
		"get public.key\r\n"
#if DT_HAS_COMPAT_STATUS_OKAY(gnss_nmea_generic) || \
    DT_HAS_COMPAT_STATUS_OKAY(u_blox_m8) || \
    DT_HAS_COMPAT_STATUS_OKAY(u_blox_f9p) || \
    DT_HAS_COMPAT_STATUS_OKAY(quectel_lcx6g) || \
    DT_HAS_COMPAT_STATUS_OKAY(quectel_lc76g) || \
    DT_HAS_COMPAT_STATUS_OKAY(luatos_air530z)
		"get gps diag\r\n"
#endif
		"get dc.restarts\r\n"
		"get tx apc\r\n"
		"get cad\r\n"
		"get bootloader.ver\r\n"
#if IS_ENABLED(CONFIG_ZEPHCORE_ROLE_REPEATER_BRIDGE)
		"get bridge.delay\r\n"
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_REPEATER_UPLINK) && IS_ENABLED(CONFIG_MQTT_LIB)
		"get uplink.status\r\n"
#endif
		"set cad.auto\r\n"
		"set cad.offset\r\n"
		"set probe.interval\r\n"
		"set cad.busycap\r\n"
		"set cad.reset\r\n"
#if IS_ENABLED(CONFIG_ZEPHCORE_ROLE_REPEATER_BRIDGE)
		"set bridge.key <32-hex>\r\n"
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_REPEATER_UPLINK) && IS_ENABLED(CONFIG_MQTT_LIB)
		"set uplink.mqtt.password <password>\r\n"
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_REPEATER_UPLINK) && IS_ENABLED(CONFIG_MQTT_LIB)
		"set uplink.wifi.psk <password>\r\n"
#endif
		"get/set adc.multiplier\r\n"
		"get/set advert.interval\r\n"
		"get/set af\r\n"
		"get/set allow.read.only\r\n"
		"get/set backoff.multiplier\r\n"
		"get/set battery.alert on|off (default on)\r\n"
		"get/set battery.group <name> (default #zephcore)\r\n"
		"get/set battery.interval <hours> (1..168, default 12)\r\n"
		"get/set battery.threshold <mV> (0..5000, default " STRINGIFY(ZEPHCORE_BATTERY_ALERT_DEFAULT_MV) "; 0=no alerts)\r\n"
#if IS_ENABLED(CONFIG_ZEPHCORE_ROLE_REPEATER_BRIDGE)
		"get/set bridge.peer <MAC> public|random\r\n"
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_ROLE_REPEATER_BRIDGE)
		"get/set bridge.priority (0..7)\r\n"
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_ROLE_REPEATER_BRIDGE) && defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
		"get/set bridge.type ble|esp-now\r\n"
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_ROLE_REPEATER_BRIDGE) && !defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
		"get/set bridge.type ble\r\n"
#endif
		"get/set display.rotate\r\n"
		"get/set dutycycle\r\n"
		"get/set extra.sf\r\n"
		"get/set flood.advert.interval\r\n"
		"get/set flood.max\r\n"
		"get/set flood.max.advert\r\n"
		"get/set flood.max.unscoped\r\n"
		"get/set freq\r\n"
		"get/set gps duty\r\n"
		"get/set guest.password\r\n"
		"get/set input.rotate\r\n"
		"get/set int.thresh\r\n"
		"get/set lat\r\n"
		"get/set leds.hb\r\n"
		"get/set leds.radio\r\n"
		"get/set lon\r\n"
		"get/set loop.detect\r\n"
		"get/set meshtimesync\r\n"
		"get/set multi.acks\r\n"
		"get/set name\r\n"
#if defined(CONFIG_BOARD_HELTEC_WIFI_LORA32_V43)
		"get/set output.power 22|28|default (nominal dBm)\r\n"
#endif
		"get/set owner.info\r\n"
		"get/set path.hash.mode\r\n"
		"get/set prv.key\r\n"
		"get/set radio\r\n"
		"get/set radio.fem.rxgain\r\n"
		"get/set radio.rxgain\r\n"
		"get/set repeat\r\n"
		"get/set rxduty\r\n"
		"get/set tx\r\n"
		"get/set tz.offset (hours)\r\n"
#if IS_ENABLED(CONFIG_ZEPHCORE_REPEATER_UPLINK) && IS_ENABLED(CONFIG_MQTT_LIB)
		"get/set uplink.enable\r\n"
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_REPEATER_UPLINK) && IS_ENABLED(CONFIG_MQTT_LIB)
		"get/set uplink.mqtt.host\r\n"
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_REPEATER_UPLINK) && IS_ENABLED(CONFIG_MQTT_LIB)
		"get/set uplink.mqtt.iata\r\n"
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_REPEATER_UPLINK) && IS_ENABLED(CONFIG_MQTT_LIB)
		"get/set uplink.mqtt.port\r\n"
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_REPEATER_UPLINK) && IS_ENABLED(CONFIG_MQTT_LIB)
		"get/set uplink.mqtt.tls\r\n"
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_REPEATER_UPLINK) && IS_ENABLED(CONFIG_MQTT_LIB)
		"get/set uplink.mqtt.user\r\n"
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_REPEATER_UPLINK) && IS_ENABLED(CONFIG_MQTT_LIB)
		"get/set uplink.wifi.ssid\r\n"
#endif
		"start dfu\r\n"
		"reboot\r\n"
		"clkreboot\r\n"
		"shutdown y\r\n"
		"erase\r\n"
		"help";
	static_assert(sizeof(repeater) <= REPEATER_CLI_TX_BUF_SIZE);
	static const char room_server[] =
		"CLI room server:\r\n"
		"setperm <permissions> <pubkey>\r\n"
		"region def|get|put|remove|list|load|save\r\n"
		"region allowf|denyf|home|default\r\n"
		"ver\r\nboard\r\nadvert\r\nadvert.zerohop\r\n"
		"clock sync\r\ntime <epoch>\r\n"
#if !defined(CONFIG_BOARD_RPI_PICO)
		"gps on|off|setloc|advert\r\nget gps diag\r\n"
		"neighbors\r\nneighbor.remove <pubkey>\r\n"
#endif
		"tempradio <freq> <bw> <sf> <cr> <minutes>\r\n"
		"password <value>\r\n"
		"stats-packets\r\nstats-radio\r\nstats-core\r\nclear stats\r\n"
		"get acl\r\nget public.key\r\nget role\r\n"
		"get dc.restarts\r\nget tx apc\r\nget cad\r\n"
		CLI_UI_RADIO_HELP
		"get/set dutycycle\r\nget/set af\r\nget/set int.thresh\r\n"
		"get/set multi.acks\r\n"
		"get/set allow.read.only\r\nget/set flood.advert.interval\r\n"
		"get/set advert.interval\r\nget/set guest.password\r\n"
		"get/set prv.key\r\nget/set name\r\nget/set repeat\r\n"
		"get/set lat\r\nget/set lon\r\nget/set radio\r\n"
		"get/set radio.rxgain\r\n"
		"get/set flood.max.advert\r\n"
		"get/set flood.max.unscoped\r\nget/set flood.max\r\n"
		"get/set backoff.multiplier\r\n"
		"get/set owner.info\r\nget/set path.hash.mode\r\n"
		"get/set loop.detect\r\nget/set tx\r\nget/set freq\r\n"
		"get/set adc.multiplier\r\nget/set rxduty\r\n"
#if !defined(CONFIG_BOARD_RPI_PICO)
		"get/set gps duty\r\n"
#endif
		"get/set meshtimesync\r\n"
		"set cad.auto\r\nset cad.offset\r\nset probe.interval\r\n"
		"set cad.busycap\r\nset cad.reset\r\n"
#if !defined(CONFIG_BOARD_RPI_PICO)
		"sensor get <key>\r\nsensor set <key> <value>\r\n"
		"sensor list start\r\n"
#endif
#if defined(CONFIG_SOC_SERIES_NRF52) || defined(CONFIG_SOC_RP2040)
		"start dfu\r\n"
#endif
#if IS_ENABLED(CONFIG_ZEPHCORE_WIFI_OTA)
		"start ota\r\nstop ota\r\n"
#endif
		"reboot\r\nclkreboot\r\nerase\r\nhelp";

	switch (role) {
	case LocalCLIHelpRole::Companion: return companion;
	case LocalCLIHelpRole::Repeater: return repeater;
	case LocalCLIHelpRole::RoomServer: return room_server;
	}
	return nullptr;
}

#undef CLI_UI_RADIO_HELP
