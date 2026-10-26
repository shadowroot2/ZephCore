#include "test.h"
#include <TxtDataHelpers.h>
#include <battery_curve.h>
#include <mesh/MeshCore.h>
#include <algorithm>
#include <cstring>

#define CONFIG_ZEPHCORE_ROLE_REPEATER_BRIDGE 1
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
#include <CompanionCLIHelp.h>
#include <CompanionCLIAudio.h>
#include <RepeaterCLIHelp.h>
#undef CONFIG_ZEPHCORE_ROLE_REPEATER_BRIDGE
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
        "help ui", "help power", "help power.2", "help sensor", "help stat",
        "help log", "help sys", "help bridge", "help bridge.cfg", "help uplink"
    };
    for (const char *command : commands) {
        const char *reply = repeater_remote_cli_help(command);
        CHECK(reply != nullptr);
        CHECK(std::strlen(reply) < 158);
    }
    CHECK(repeater_remote_cli_help("help unknown") == nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help"), "bridge.cfg") != nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help"), "uplink") != nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help id"), "get prv.key") == nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help rf"), "set freq") == nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help rf"), "radio.rxgain") != nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help rf"), "tempradio") != nullptr);
    CHECK(repeater_remote_cli_help("help radio") == nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help id"), "guest.password") != nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help mesh"), "flood.") != nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help mesh"), "backoff.multiplier") != nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help find"), "discover.neighbors") != nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help find"), "flood.") == nullptr);
    CHECK(repeater_remote_cli_help("help mesh.2") == nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help access"), "setperm") != nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help access"), "password") == nullptr);
    CHECK(repeater_remote_cli_help("help flood") == nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help power"), "help power.2") != nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help power.2"), "battery.threshold") != nullptr);
    CHECK(repeater_remote_cli_help("help battery") == nullptr);
    CHECK(std::strstr(repeater_remote_cli_help("help sys"), "shutdown y") != nullptr);
}
TEST(companion_cli_help, "UNIT-CLI-002", "Companion app and remote help are categorized and frame bounded") {
    const char *app_commands[] = {
        "help", "?", "help id", "help rf", "help cad", "help mesh", "help gps",
        "help wifi", "help extra", "help ui", "help power", "help stat", "help sys"
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
	const char *local_only[] = {
		"get wifi.pwd", "set wifi.pwd secret", "set wifi.ssid network", "set wifi.clear",
		"set pin 123456", "set freq 869.525", "set radio 869.525,125,9,5",
		"set tx 22", "set adc.multiplier 1.0", "set autoshutdown 3200",
		"shutdown", "shutdown y", "poweroff", "offgrid on", "offgrid off",
		"get wifi.ssid", "clear stats",
	};
	for (const char *command : local_only) {
		CHECK(!companion_remote_cli_command_allowed(command));
	}
	const char *remote_allowed[] = {
		"set wifi.enabled 0", "get freq", "get radio", "get tx",
		"set af 3", "get adc.multiplier", "get autoshutdown", "reboot",
	};
	for (const char *command : remote_allowed) {
		CHECK(companion_remote_cli_command_allowed(command));
	}
}
TEST(companion_cli_audio, "UNIT-CLI-003", "Remote companion CLI suppresses operation notifications") {
	CHECK(!companion_cli_silent_notifications(0));
	CHECK(companion_cli_silent_notifications(1));
	CHECK(companion_cli_silent_notifications(0xFFFFFFFFU));
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
