#include "test.h"
#include <adapters/sensors/ZephyrSensorManager.h>
#include <vector>

namespace {
bool available, enabled, local_light, external_light;
gps_state_info info;
unsigned gps_control_calls;
void reset() {
    available = enabled = true;
    local_light = external_light = false;
    gps_control_calls = 0;
    info = {};
    info.visible_satellites = 17;
    info.satellites = 8;
}
std::vector<uint8_t> query(uint8_t permissions = 7) {
    CayenneLPP l(64);
    CHECK(sensors.querySensors(permissions, l));
    return {l.getBuffer(), l.getBuffer() + l.getSize()};
}
}
extern "C" int gps_manager_init() { return 0; }
extern "C" bool gps_is_available() { return available; }
extern "C" bool gps_is_enabled() { return enabled; }
extern "C" void gps_enable(bool value) { ++gps_control_calls; enabled = value; }
extern "C" void gps_set_poll_interval_sec(uint32_t) { ++gps_control_calls; }
extern "C" bool gps_get_last_known_position(gps_position *) { return false; }
extern "C" void gps_get_state_info(gps_state_info *out) { *out = info; }
extern "C" int env_sensors_init() { return 0; }
extern "C" int env_sensor_count() { return local_light || external_light ? 1 : 0; }
extern "C" bool env_sensor_is_board_local(int) { return local_light; }
extern "C" int env_sensor_read(int, env_sensor_reading *out) {
    *out = {};
    out->fields = ENV_F_LUMINOSITY;
    out->luminosity = 512;
    return 0;
}

TEST(sensor_satellites_wire, "UNIT-SENSOR-001", "Satellite luminosity uses visible count, GGA fallback and a limit of 32") {
    reset();
    CHECK(query() == std::vector<uint8_t>({1, 101, 0, 17}));
    info.visible_satellites = 0;
    CHECK(query() == std::vector<uint8_t>({1, 101, 0, 8}));
    info.visible_satellites = 100;
    CHECK(query() == std::vector<uint8_t>({1, 101, 0, 32}));
    info.visible_satellites = info.satellites = 0;
    CHECK(query() == std::vector<uint8_t>({1, 101, 0, 0}));
}
TEST(sensor_satellites_permissions, "UNIT-SENSOR-002", "Satellite count requires location and environment permissions") {
    reset();
    for (uint8_t mask = 0; mask < 8; ++mask) {
        auto data = query(mask);
        if ((mask & 6) == 6) CHECK(data == std::vector<uint8_t>({1, 101, 0, 17}));
        else CHECK(data.empty());
    }
}
TEST(sensor_satellites_power, "UNIT-SENSOR-003", "Telemetry reads cached satellite count without changing GPS state") {
    reset();
    enabled = false;
    CHECK(query() == std::vector<uint8_t>({1, 101, 0, 17}));
    CHECK(!enabled);
    CHECK(gps_control_calls == 0);
    enabled = true;
    available = false;
    CHECK(query() == std::vector<uint8_t>({1, 101, 0, 17}));
    CHECK(!available && enabled);
    CHECK(gps_control_calls == 0);
}
TEST(sensor_real_light, "UNIT-SENSOR-004", "Real board-local light takes precedence and external light keeps its own channel") {
    reset();
    local_light = true;
    CHECK(query() == std::vector<uint8_t>({1, 101, 2, 0}));
    local_light = false;
    external_light = true;
    CHECK(query() == std::vector<uint8_t>({2, 101, 2, 0, 1, 101, 0, 17}));
}
