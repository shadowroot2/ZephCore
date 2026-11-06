#include "test.h"
#include <TxtDataHelpers.h>
#include <battery_curve.h>
#include <mesh/MeshCore.h>
#include <algorithm>
#include <cstring>

#define CONFIG_ZEPHCORE_ROLE_REPEATER_BRIDGE 1
#define CONFIG_LOG 1
#define CONFIG_POWEROFF 1
#define CONFIG_ZEPHCORE_CLI_DFU 1
#define CONFIG_ZEPHCORE_CLI_PWRMGT 1
#define CONFIG_ZEPHCORE_CLI_POWERSAVING 1
#define CONFIG_ZEPHCORE_REPEATER_UPLINK 1
#define CONFIG_MQTT_LIB 1
#define CONFIG_ZEPHCORE_RADIO_LR2021 1
#define CONFIG_ZEPHCORE_UI_DISPLAY 1
#define CONFIG_ZEPHCORE_WIFI_OTA 1
#define CONFIG_ZEPHCORE_MEM_STATS 1
#define CONFIG_ZEPHCORE_POWER_DIAG 1
#define CONFIG_ZEPHCORE_LOOP_WAKE_STATS 1
#define CONFIG_ZEPHCORE_COMPANION_WIFI 1
#define ZEPHCORE_FALL_DETECTOR 1
/* Host stand-in for the DT GNSS capability check used by the nav group. */
#define DT_HAS_COMPAT_STATUS_OKAY(x) 1
#include <CompanionCLIHelp.h>
#include <CompanionCLIAudio.h>
#include <CompanionSOSPolicy.h>
#include <GpsSwitchPolicy.h>
#include <TrackingMessage.h>
#include <GeoFence.h>
#include <RepeaterCLIHelp.h>
#undef CONFIG_ZEPHCORE_ROLE_REPEATER_BRIDGE
#undef CONFIG_LOG
#undef CONFIG_POWEROFF
#undef CONFIG_ZEPHCORE_CLI_DFU
#undef CONFIG_ZEPHCORE_CLI_PWRMGT
#undef CONFIG_ZEPHCORE_CLI_POWERSAVING
#undef CONFIG_ZEPHCORE_REPEATER_UPLINK
#undef CONFIG_MQTT_LIB
#undef CONFIG_ZEPHCORE_RADIO_LR2021
#undef CONFIG_ZEPHCORE_UI_DISPLAY
#undef CONFIG_ZEPHCORE_WIFI_OTA
#undef CONFIG_ZEPHCORE_MEM_STATS
#undef CONFIG_ZEPHCORE_POWER_DIAG
#undef CONFIG_ZEPHCORE_LOOP_WAKE_STATS
#undef CONFIG_ZEPHCORE_COMPANION_WIFI
#undef ZEPHCORE_FALL_DETECTOR

TEST(string_capacity, "UNIT-STRING-001", "String copies terminate and respect zero exact and short capacities") {
    for (size_t size = 0; size <= 10; ++size) {
        std::vector<char> out(size + 2, '#');
        StrHelper::strncpy(out.data() + 1, "abcd", size);
        CHECK(out.front() == '#' && out.back() == '#');
        if (size) CHECK(std::string(out.data() + 1) == std::string("abcd").substr(0, size - 1));
        std::fill(out.begin(), out.end(), '#');
        StrHelper::strzcpy(out.data() + 1, "abcd", size);
        CHECK(out.front() == '#' && out.back() == '#');
        if (size) {
            auto copied = std::min(size_t(4), size - 1);
            CHECK(std::string(out.data() + 1) == std::string("abcd").substr(0, copied));
            for (size_t i = copied; i < size; ++i) CHECK(out[i + 1] == 0);
        }
    }
}
TEST(string_normalization, "UNIT-STRING-002", "Decimal trimming and blank classification") {
    const char *input[] = {"250.000", "62.500", "0.0010", "-10.000", "1000", "", "1.25"};
    const char *expected[] = {"250", "62.5", "0.001", "-10", "1000", "", "1.25"};
    for (unsigned i = 0; i < 7; ++i) {
        char buffer[32]; std::strcpy(buffer, input[i]); StrHelper::stripTrailingZeros(buffer);
        CHECK(std::strcmp(buffer, expected[i]) == 0);
    }
    CHECK(StrHelper::isBlank(nullptr) && StrHelper::isBlank("") && StrHelper::isBlank(" \t\r\n"));
    CHECK(!StrHelper::isBlank(" a ") && !StrHelper::isBlank("0"));
}
TEST(rtc_unique, "UNIT-RTC-001", "Unique timestamps survive repeated and backward wall time") {
    struct RTC : mesh::RTCClock {
        uint32_t now = 100;
        uint32_t getCurrentTime() override { return now; }
        void setCurrentTime(uint32_t value) override { now = value; }
    } rtc;
    CHECK(rtc.getCurrentTimeUnique() == 100); CHECK(rtc.getCurrentTimeUnique() == 101);
    rtc.now = 50; CHECK(rtc.getCurrentTimeUnique() == 102);
    rtc.now = 200; CHECK(rtc.getCurrentTimeUnique() == 200);
}
TEST(repeater_remote_help, "UNIT-CLI-001", "Repeater remote help is categorized and packet bounded") {
    const char *commands[] = {
        "help", "?", "help id", "help rf", "help cad",
        "help mesh", "help find", "help region", "help access", "help gps",
		"help ui", "help power", "help batt", "help sensor", "help stat",
		"help log", "help sys", "help bridge", "help uplink"
    };
    for (const char *command : commands) {
        const char *reply = repeater_remote_cli_help(command);
        CHECK(reply != nullptr);
        CHECK(std::strlen(reply) < 158);
    }
    CHECK(repeater_remote_cli_help("help unknown") == nullptr);
	CHECK(repeater_cli_ota_command("start ota"));
	CHECK(repeater_cli_ota_command("stop ota"));
	CHECK(repeater_cli_ota_command("start ota now"));
	CHECK(!repeater_cli_ota_command("start dfu"));
	CHECK(std::strstr(repeater_remote_cli_help("help"), "bridge.cfg") == nullptr);
	CHECK(std::strstr(repeater_remote_cli_help("help bridge"), "set bridge.key") != nullptr);
	CHECK(std::strstr(repeater_remote_cli_help("help bridge"), "bridge [on|off]\n") ==
	      repeater_remote_cli_help("help bridge"));
	CHECK(std::strstr(repeater_remote_cli_help("help bridge"),
	                  "get/set:\nbridge.peer\nbridge.priority\nbridge.type") != nullptr);
	CHECK(std::strstr(repeater_remote_cli_help("help bridge", false), "bridge unpair") == nullptr);
	CHECK(std::strstr(repeater_remote_cli_help("help bridge", false), "bridge logs") == nullptr);
	CHECK(std::strstr(repeater_remote_cli_help("help bridge", false), "bridge bonds") == nullptr);
	CHECK(std::strstr(repeater_remote_cli_help("help bridge", false), "bridge ping") != nullptr);
	CHECK(repeater_remote_cli_help("help bridge.cfg") == nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help"), "uplink") != nullptr);
	const char *groups_help = repeater_remote_cli_help("help");
	CHECK(std::strstr(groups_help, "id") < std::strstr(groups_help, "access"));
	CHECK(std::strstr(groups_help, "access") < std::strstr(groups_help, "rf"));
	CHECK(std::strstr(groups_help, "rf") < std::strstr(groups_help, "region"));
	CHECK(std::strstr(groups_help, "region") < std::strstr(groups_help, "cad"));
	CHECK(std::strstr(groups_help, "bridge") < std::strstr(groups_help, "stat"));
	CHECK(std::strstr(groups_help, "uplink") < std::strstr(groups_help, "stat"));
	CHECK(std::strstr(groups_help, "stat") < std::strstr(groups_help, "log"));
	CHECK(std::strstr(groups_help, "log") < std::strstr(groups_help, "sys"));
    CHECK(std::strstr(repeater_remote_cli_help("help id"), "get prv.key") == nullptr);
	const char *id_help = repeater_remote_cli_help("help id");
	CHECK(std::strstr(id_help, "password") == nullptr);
	CHECK(std::strstr(id_help, "get public.key") < std::strstr(id_help, "set prv.key"));
	const char *access_help = repeater_remote_cli_help("help access");
	CHECK(std::strstr(access_help, "password <value>") != nullptr);
	CHECK(std::strstr(access_help, "get/set guest.password") != nullptr);
	CHECK(std::strstr(access_help, "setperm") != nullptr);
	const char *gps_help = repeater_remote_cli_help("help gps");
	CHECK(std::strstr(gps_help, "gps on|off\n") != nullptr);
	CHECK(std::strstr(gps_help, "gps sync|setloc|advert") != nullptr);
	CHECK(std::strstr(gps_help, "\nget gps\n") == nullptr);
	CHECK(std::strcmp(repeater_remote_cli_help("help log"), "log start\nlog stop") == 0);
    CHECK(std::strstr(repeater_remote_cli_help("help rf"), "set freq") == nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help rf"), "radio.rxgain") != nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help rf"), "tempradio") != nullptr);
	const char *rf_help = repeater_remote_cli_help("help rf");
	CHECK(std::strstr(rf_help, "get freq\nget freqerr\n") == rf_help);
	const char *cad_help = repeater_remote_cli_help("help cad");
	CHECK(std::strstr(cad_help, "get cad.stats\nget txdelay\nset cad.reset\n") != nullptr);
	CHECK(std::strstr(cad_help, "set cad.reset") < std::strstr(cad_help, "get cad.auto"));
	CHECK(std::strstr(repeater_remote_cli_help("help mesh"), "advert[.zerohop]\nget/set:\nrepeat\n") ==
	      repeater_remote_cli_help("help mesh"));
	CHECK(std::strstr(repeater_remote_cli_help("help mesh"),
	                  "flood.{advert.interval,max,max.advert,max.unscoped}\n") != nullptr);
	CHECK(std::strstr(repeater_remote_cli_help("help find"), "discover.neighbors\n") ==
	      repeater_remote_cli_help("help find"));
	CHECK(std::strstr(repeater_remote_cli_help("help uplink"),
	                  "get uplink.status\nget/set uplink.enable\nset uplink.wifi.psk\n") ==
	      repeater_remote_cli_help("help uplink"));
    CHECK(repeater_remote_cli_help("help radio") == nullptr);
	CHECK(std::strstr(repeater_remote_cli_help("help id"), "guest.password") == nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help mesh"), "flood.") != nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help mesh"), "backoff.multiplier") != nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help find"), "discover.neighbors") != nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help find"), "flood.") == nullptr);
    CHECK(repeater_remote_cli_help("help mesh.2") == nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help access"), "setperm") != nullptr);
	CHECK(std::strstr(repeater_remote_cli_help("help sys"), "start ota") == nullptr);
	CHECK(std::strstr(repeater_remote_cli_help("help sys"), "stop ota") == nullptr);
    CHECK(repeater_remote_cli_help("help flood") == nullptr);
	CHECK(std::strstr(repeater_remote_cli_help("help"), "batt") != nullptr);
	CHECK(std::strstr(repeater_remote_cli_help("help power"), "battery.threshold") == nullptr);
	CHECK(std::strstr(repeater_remote_cli_help("help batt"), "battery.threshold") != nullptr);
	CHECK(repeater_remote_cli_help("help battery") == nullptr);
	CHECK(repeater_remote_cli_help("help power.2") == nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help sys"), "shutdown y") != nullptr);
	CHECK(std::strstr(repeater_remote_cli_help("help sys"), "clkreboot") == nullptr);
}
TEST(companion_cli_help, "UNIT-CLI-002", "Companion app and remote help are categorized and frame bounded") {
    const char *app_commands[] = {
        "help", "?", "help id", "help rf", "help cad", "help mesh", "help gps",
        "help wifi", "help nav", "help geo", "help extra", "help ui", "help power", "help stat", "help sys"
    };
    for (const char *command : app_commands) {
        const char *reply = companion_app_cli_help(command);
        CHECK(reply != nullptr);
        CHECK(std::strlen(reply) <= 172);
    }
	for (const char *command : app_commands) {
		const char *reply = companion_remote_cli_help(command);
		CHECK(reply != nullptr);
		CHECK(std::strlen(reply) < 158);
	}
	CHECK(companion_remote_cli_help("help unknown") == nullptr);
	CHECK(std::strstr(companion_remote_cli_help("help id"), "prv.key") == nullptr);
	CHECK(std::strstr(companion_remote_cli_help("help extra"), "v.contact") != nullptr);
	CHECK(std::strstr(companion_remote_cli_help("help extra"), "offgrid") == nullptr);
	CHECK(std::strstr(companion_remote_cli_help("help wifi"), "wifi.ssid") == nullptr);
	CHECK(std::strstr(companion_remote_cli_help("help stat"), "clear stats") == nullptr);
	CHECK(std::strstr(companion_remote_cli_help("help sys"), "start ota") == nullptr);
	CHECK(std::strstr(companion_remote_cli_help("help sys"), "start dfu") == nullptr);
	CHECK(std::strstr(companion_remote_cli_help("help sys"), "erase") == nullptr);
	CHECK(std::strstr(companion_remote_cli_help("help geo"), "geo save") == nullptr);
	CHECK(std::strstr(companion_remote_cli_help("help geo"), "43.241117") != nullptr);
	const char *local_only[] = {
		"get wifi.pwd", "set wifi.pwd secret", "set wifi.ssid network", "set wifi.clear",
		"set pin 123456", "set freq 869.525", "set radio 869.525,125,9,5",
		"set tx 22", "set adc.multiplier 1.0", "set autoshutdown 3200",
		"shutdown", "shutdown y", "poweroff", "offgrid on", "offgrid off", "wp clear", "wp 1", "nav start", "nav stop",
		"wp save", "wp save 2", "wp save 2 Base", "wp name 2 TEST", "wp name TEST",
		"nav telemetry", "nav telemetry on", "nav telemetry off",
		"get nav.telemetry.interval", "set nav.telemetry.interval 10",
		"get nav.interval", "set nav.interval 2",
		"geo save",
		"get geo.radius", "set geo.radius 3000",
		"get wifi.ssid", "clear stats",
	};
	for (const char *command : local_only) {
		CHECK(!companion_remote_cli_command_allowed(command));
	}
	const char *remote_allowed[] = {
		"set wifi.enabled 0", "get freq", "get radio", "get tx",
		"set af 3", "get adc.multiplier", "get autoshutdown", "reboot",
		"buzz", "leds", "gps", "tracking", "nav", "wp list",
		"geo", "geo on", "geo off", "geo log", "get geo.point",
		"set geo.point 43.241117,76.927301",
	};
	for (const char *command : remote_allowed) {
		CHECK(companion_remote_cli_command_allowed(command));
	}
}
TEST(companion_cli_alias, "UNIT-CLI-004", "Bare buzz and leds aliases report status via get") {
	CHECK(std::strcmp(companion_cli_alias_command("leds on"), "set leds on") == 0);
	CHECK(std::strcmp(companion_cli_alias_command("leds off"), "set leds off") == 0);
	CHECK(std::strcmp(companion_cli_alias_command("leds"), "get leds") == 0);
	CHECK(std::strcmp(companion_cli_alias_command("buzz on"), "set buzzer on") == 0);
	CHECK(std::strcmp(companion_cli_alias_command("buzz off"), "set buzzer off") == 0);
	CHECK(std::strcmp(companion_cli_alias_command("buzz"), "get buzzer") == 0);
	CHECK(companion_cli_alias_command("leds.hb") == nullptr);
	CHECK(companion_cli_alias_command("buzzer") == nullptr);
	CHECK(companion_cli_alias_command("buzz on extra") == nullptr);
}
TEST(companion_cli_audio, "UNIT-CLI-003", "Remote companion CLI suppresses operation notifications") {
	CHECK(!companion_cli_silent_notifications(0));
	CHECK(companion_cli_silent_notifications(1));
	CHECK(companion_cli_silent_notifications(0xFFFFFFFFU));
}
TEST(companion_sos_cancel, "UNIT-SOS-001", "Only a pending manual SOS is button-cancelable") {
	CHECK(!companion_sos_button_cancelable(false, false));
	CHECK(companion_sos_button_cancelable(true, false));
	CHECK(!companion_sos_button_cancelable(true, true));
	CHECK(std::strcmp(COMPANION_SOS_CANCELED_TEXT, "❌ SOS отменён") == 0);
}
TEST(tracking_report_format, "UNIT-TRACK-001", "Tracking report carries speed and course only while moving") {
	char buf[160];
	tracking_format_report(buf, sizeof(buf), 55.75580, 37.61730, 3333, 45000);
	CHECK(std::strcmp(buf, "🏃 12 км/ч ↗️ СВ\n📍 https://maps.google.com/?q=55.75580,37.61730") == 0);
	tracking_format_report(buf, sizeof(buf), 55.75580, 37.61730, 0, 0);
	CHECK(std::strcmp(buf, "🅿️\n📍 https://maps.google.com/?q=55.75580,37.61730") == 0);
	tracking_format_report(buf, sizeof(buf), 55.75580, 37.61730, 555, 180000);
	CHECK(std::strcmp(buf, "🅿️\n📍 https://maps.google.com/?q=55.75580,37.61730") == 0);
	CHECK(std::strcmp(tracking_report_arrow(0), "⬆️") == 0);
	CHECK(std::strcmp(tracking_report_sector(0), "С") == 0);
	CHECK(std::strcmp(tracking_report_sector(22500), "СВ") == 0);
	CHECK(std::strcmp(tracking_report_sector(112500), "ЮВ") == 0);
	CHECK(std::strcmp(tracking_report_sector(202500), "ЮЗ") == 0);
	CHECK(std::strcmp(tracking_report_sector(359999), "С") == 0);
	CHECK(std::strcmp(tracking_report_sector(360000), "С") == 0);
}
TEST(tracking_odometer_stats, "UNIT-TRACK-002", "Odometer counts moving distance, stops and max speed per session") {
	struct tracking_odometer o;
	tracking_odometer_reset(&o, 0);
	tracking_odometer_on_fix(&o, 60000, 55000000000LL, 37000000000LL, 0);
	CHECK(o.distance_mm == 0 && o.stopped_ms == 60000 && o.stops == 0);
	tracking_odometer_on_fix(&o, 120000, 55000900000LL, 37000000000LL, 5000);
	CHECK(o.moving_ms == 60000 && o.stops == 0);
	CHECK(o.distance_mm > 95000 && o.distance_mm < 105000);
	CHECK(o.max_speed_mms == 5000);
	tracking_odometer_on_fix(&o, 180000, 55000900000LL, 37000000000LL, 0);
	CHECK(o.stops == 1 && o.stopped_ms == 120000);
	tracking_odometer_finish(&o, 240000);
	CHECK(o.stops == 1 && o.stopped_ms == 180000 && o.moving_ms == 60000);
}
TEST(tracking_summary_format, "UNIT-TRACK-003", "Session summary fits the mesh frame and pins the format") {
	struct tracking_odometer o;
	memset(&o, 0, sizeof(o));
	char buf[160];
	tracking_format_summary(buf, sizeof(buf), &o, 55751240000LL, 37618420000LL, true);
	CHECK(std::strcmp(buf, "🏁 0.0км (0.0 / 0 км/ч)\n⏱️ 0ч00м\n🚶 0ч00м\n"
		"🅿️ 0 мин (0)\n"
		"📍 https://maps.google.com/?q=55.75124,37.61842") == 0);
	o.distance_mm = 999900000;
	o.moving_ms = 45300000;
	o.stopped_ms = 35700000;
	o.stops = 99;
	o.max_speed_mms = 34445;
	tracking_format_summary(buf, sizeof(buf), &o, -123456780000LL, -89123450000LL, true);
	CHECK(std::strlen(buf) <= 159);
	CHECK(std::strstr(buf, "999.9км") != nullptr);
	CHECK(std::strstr(buf, "км/ч") != nullptr);
	CHECK(std::strstr(buf, "-123.45678,-89.12345") != nullptr);
	tracking_format_summary(buf, sizeof(buf), &o, 0, 0, false);
	CHECK(std::strstr(buf, "maps.google.com") == nullptr);
}
TEST(nav_format, "UNIT-NAV-001", "Distance, bearing and cardinal labels for navigation") {
	char buf[24];
	nav_distance_str(buf, sizeof(buf), 840.4);
	CHECK(std::strcmp(buf, "840 m") == 0);
	nav_distance_str(buf, sizeof(buf), 2341.0);
	CHECK(std::strcmp(buf, "2.34 km") == 0);
	nav_distance_str(buf, sizeof(buf), 1000.0);
	CHECK(std::strcmp(buf, "1.00 km") == 0);
	double north = nav_bearing_deg(43.241117, 76.927301, 43.251117, 76.927301);
	CHECK(north > 350.0 || north < 10.0);
	double east = nav_bearing_deg(43.241117, 76.927301, 43.241117, 76.937301);
	CHECK(east > 80.0 && east < 100.0);
	CHECK(std::strcmp(nav_cardinal_en(0), "N") == 0);
	CHECK(std::strcmp(nav_cardinal_en(46), "NE") == 0);
	CHECK(std::strcmp(nav_cardinal_en(180), "S") == 0);
	CHECK(std::strcmp(nav_cardinal_en(359), "N") == 0);
}
TEST(nav_wrongway, "UNIT-NAV-002", "Wrong-way ladder steps at 30 m and resets on approach") {
	struct nav_wrongway w;
	nav_wrongway_reset(&w);
	CHECK(!nav_wrongway_update(&w, 100.0));
	CHECK(!nav_wrongway_update(&w, 110.0));
	CHECK(nav_wrongway_update(&w, 145.0));
	CHECK(!nav_wrongway_update(&w, 150.0));
	CHECK(nav_wrongway_update(&w, 185.0));
	CHECK(!nav_wrongway_update(&w, 180.0));
	CHECK(!nav_wrongway_update(&w, 200.0));
	CHECK(nav_wrongway_update(&w, 235.0));
	nav_wrongway_reset(&w);
	nav_wrongway_update(&w, 100.0);
	CHECK(!nav_wrongway_update(&w, 102.0));
	CHECK(!nav_wrongway_update(&w, 101.0));
	CHECK(!nav_wrongway_update(&w, 103.0));
	CHECK(!nav_wrongway_update(&w, 102.0));
}
TEST(nav_report, "UNIT-NAV-003", "Periodic vContact report carries distance, sector and arrow") {
	char buf[32];
	nav_report_str(buf, sizeof(buf), 2341.0, 46.0);
	CHECK(std::strcmp(buf, "2.34 км  СВ  ↗️") == 0);
	nav_report_str(buf, sizeof(buf), 840.4, 0.0);
	CHECK(std::strcmp(buf, "840 м  С  ⬆️") == 0);
	CHECK(std::strlen(buf) <= 159);
}
TEST(geofence_fsm, "UNIT-GEO-001", "Exit fires on the first fix beyond, return at hysteresis and re-arms") {
	struct geofence_state g;
	geofence_reset(&g);
	CHECK(geofence_update(&g, 100.0, 3000.0) == GEO_EVENT_NONE);
	CHECK(geofence_update(&g, 3000.0, 3000.0) == GEO_EVENT_NONE);
	/* First fix beyond the radius fires the exit, exactly once. */
	CHECK(geofence_update(&g, 3100.0, 3000.0) == GEO_EVENT_EXIT);
	CHECK(geofence_update(&g, 3500.0, 3000.0) == GEO_EVENT_NONE);
	/* Return needs the hysteresis band: 2980 is still out. */
	CHECK(geofence_update(&g, 2980.0, 3000.0) == GEO_EVENT_NONE);
	CHECK(geofence_update(&g, 2940.0, 3000.0) == GEO_EVENT_REENTER);
	/* Re-armed: the next beyond fix fires a second exit. */
	CHECK(geofence_update(&g, 3100.0, 3000.0) == GEO_EVENT_EXIT);
}
TEST(geofence_boot, "UNIT-GEO-005", "After boot the first fix reports the zone state exactly once") {
	struct geofence_state g;
	geofence_boot_arm(&g);
	/* First fix inside: one return message, then quiet. */
	CHECK(geofence_update(&g, 100.0, 3000.0) == GEO_EVENT_REENTER);
	CHECK(geofence_update(&g, 100.0, 3000.0) == GEO_EVENT_NONE);
	/* First fix beyond: one exit message, then quiet. */
	geofence_boot_arm(&g);
	CHECK(geofence_update(&g, 3100.0, 3000.0) == GEO_EVENT_EXIT);
	CHECK(geofence_update(&g, 3100.0, 3000.0) == GEO_EVENT_NONE);
	/* No boot arm: silent inside. */
	geofence_reset(&g);
	CHECK(geofence_update(&g, 100.0, 3000.0) == GEO_EVENT_NONE);
}
TEST(geofence_msg, "UNIT-GEO-002", "Exit and return messages pin the template and fit the send budget") {
	char buf[160];
	geofence_format_msg(buf, sizeof(buf), true, 3000.0, 3500.0, 43.24112, 76.92730);
	CHECK(std::strcmp(buf, "🚪 Вышел из геозоны\n📐 3км\n📏 3.5км\n"
			      "📍 https://maps.google.com/?q=43.24112,76.92730") == 0);
	/* Worst-case budget: MAX_TEXT_LEN 160 minus a 31-char node name and
	 * the ": " the send path prefixes; the map link must survive it. */
	CHECK(std::strlen(buf) <= 127);
	geofence_format_msg(buf, sizeof(buf), false, 3000.0, 2900.0, 43.24112, 76.92730);
	CHECK(std::strcmp(buf, "✅ Вернулся в геозону\n📐 3км\n📏 2.9км\n"
			      "📍 https://maps.google.com/?q=43.24112,76.92730") == 0);
	CHECK(std::strlen(buf) <= 127);
	/* Tight budget: the radius line goes first, the link stays whole. */
	geofence_format_msg(buf, 100, true, 3000.0, 3500.0, 43.24112, 76.92730);
	CHECK(std::strstr(buf, "📐") == nullptr);
	CHECK(std::strstr(buf, "https://maps.google.com/?q=43.24112,76.92730") != nullptr);
	CHECK(std::strlen(buf) + 1 <= 100);
}
TEST(geofence_parse, "UNIT-GEO-003", "set geo.point accepts exactly two in-range numbers") {
	double lat, lon;
	CHECK(geofence_parse_point("43.24112,76.92730", &lat, &lon));
	CHECK(lat > 43.241 && lat < 43.242 && lon > 76.927 && lon < 76.928);
	CHECK(geofence_parse_point("  -45.5  -120.25  ", &lat, &lon));
	CHECK(lat == -45.5 && lon == -120.25);
	CHECK(geofence_parse_point("90 180", &lat, &lon));
	CHECK(!geofence_parse_point("91 0", &lat, &lon));
	CHECK(!geofence_parse_point("0 181", &lat, &lon));
	CHECK(!geofence_parse_point("abc def", &lat, &lon));
	CHECK(!geofence_parse_point("43.2", &lat, &lon));
	CHECK(!geofence_parse_point("43.2 76.9 1", &lat, &lon));
	CHECK(!geofence_parse_point("", &lat, &lon));
}
TEST(geofence_defaults, "UNIT-GEO-004", "The default point is Almaty 43.241117 76.927301 for home and geo") {
	CHECK(GEO_LAT7_DEFAULT == 432411170);
	CHECK(GEO_LON7_DEFAULT == 769273010);
	CHECK(ZC_POINT_DEFAULT_LAT_E7 / 1e7 == 43.241117);
	CHECK(ZC_POINT_DEFAULT_LON_E7 / 1e7 == 76.927301);
}
TEST(companion_wp_save, "UNIT-CLI-005", "wp save parses slot and name and pins the 10-char limit") {
	int slot;
	char name[12];
	CHECK(companion_wp_save_parse("", 4, &slot, name, sizeof(name)) == NULL);
	CHECK(slot == 4);
	CHECK(std::strcmp(name, "") == 0);
	CHECK(companion_wp_save_parse("  ", 4, &slot, name, sizeof(name)) == NULL);
	CHECK(slot == 4);
	CHECK(companion_wp_save_parse("3", 4, &slot, name, sizeof(name)) == NULL);
	CHECK(slot == 2);
	CHECK(companion_wp_save_parse("3 My House", 4, &slot, name, sizeof(name)) == NULL);
	CHECK(slot == 2);
	CHECK(std::strcmp(name, "My House") == 0);
	CHECK(companion_wp_save_parse(" 2 Base  ", 4, &slot, name, sizeof(name)) == NULL);
	CHECK(slot == 1);
	CHECK(std::strcmp(name, "Base") == 0);
	CHECK(companion_wp_save_parse("Base", 4, &slot, name, sizeof(name)) == NULL);
	CHECK(slot == 4);
	CHECK(std::strcmp(name, "Base") == 0);
	CHECK(companion_wp_save_parse("10 1234567890", 4, &slot, name, sizeof(name)) == NULL);
	CHECK(slot == 9);
	CHECK(std::strcmp(name, "1234567890") == 0);
	CHECK(std::strcmp(companion_wp_save_parse("0", 4, &slot, name, sizeof(name)),
			  "Error: wp 1..10") == 0);
	CHECK(std::strcmp(companion_wp_save_parse("11", 4, &slot, name, sizeof(name)),
			  "Error: wp 1..10") == 0);
	CHECK(std::strcmp(companion_wp_save_parse("3.5", 4, &slot, name, sizeof(name)),
			  "Error: wp 1..10") == 0);
	CHECK(std::strcmp(companion_wp_save_parse("1 Base", 4, &slot, name, sizeof(name)),
			  "Error: point 1 stays GPS HOME") == 0);
	CHECK(std::strcmp(companion_wp_save_parse("3 12345678901", 4, &slot, name, sizeof(name)),
			  "Error: name max 10 chars") == 0);
}
TEST(gps_switch_policy, "UNIT-UI-001", "A physical GPS switch overrides the saved startup state") {
	CHECK(gps_switch_effective_enabled(false, false, true));
	CHECK(!gps_switch_effective_enabled(false, true, false));
	CHECK(!gps_switch_effective_enabled(true, false, true));
	CHECK(gps_switch_effective_enabled(true, true, false));
}
TEST(battery_curve, "UNIT-BATTERY-001", "Battery endpoints interpolation multicell scaling and monotonicity") {
    CHECK(battery_curve_lookup(&battery_curve_default, 0) == 0);
    CHECK(battery_curve_lookup(&battery_curve_default, 3100) == 0);
    CHECK(battery_curve_lookup(&battery_curve_default, 4190) == 100);
    CHECK(battery_curve_lookup(&battery_curve_default, 3720) == 50);
    const uint16_t points[] = {4200, 3600, 3000};
    battery_curve_t twoCell{points, 3, 2};
    CHECK(battery_curve_lookup(&twoCell, 7800) == 75);
    CHECK(battery_curve_lookup(&twoCell, 6600) == 25);
    unsigned previous = 0;
    for (unsigned mv = 0; mv <= 65535; ++mv) {
        auto percent = battery_curve_lookup(&battery_curve_default, mv);
        CHECK(percent >= previous && percent <= 100); previous = percent;
    }
}
