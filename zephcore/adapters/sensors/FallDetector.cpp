/* SPDX-License-Identifier: MIT */

#include "FallDetector.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(zephcore_fall, CONFIG_ZEPHCORE_MAIN_LOG_LEVEL);

namespace {

constexpr uint8_t I2C_ADDR_SC7A20 = 0x19;
constexpr uint8_t I2C_ADDR_QMA6100 = 0x12;
constexpr uint32_t SAMPLE_PERIOD_MS = 20;
constexpr uint32_t FREE_FALL_MAX_MG = 650;
constexpr uint32_t IMPACT_MIN_MG = 2800;
constexpr uint32_t FREE_FALL_WINDOW_MS = 600;
constexpr uint32_t SETTLE_START_MS = 1000;
constexpr uint32_t SETTLE_WINDOW_MS = 8000;
constexpr uint32_t STILL_BAND_MG = 200;
constexpr uint32_t FALL_COOLDOWN_MS = 60000;

enum detector_state {
	DETECTOR_IDLE,
	DETECTOR_FREE_FALL,
	DETECTOR_SETTLING,
	DETECTOR_COOLDOWN,
};

static const struct device *const i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));
#if defined(CONFIG_BOARD_THINKNODE_M3)
static const struct device *const accel_power = DEVICE_DT_GET(DT_ALIAS(accel_power));
#endif

static struct k_work_delayable sample_work;
static fall_detector_callback_t detected_callback;
static enum detector_state state;
static uint32_t state_started_ms;
static uint32_t still_samples;
static uint32_t settle_samples;
static bool available;

static uint32_t isqrt_u32(uint32_t value)
{
	uint32_t result = 0;
	uint32_t bit = 1U << 30;

	while (bit > value) {
		bit >>= 2;
	}
	while (bit != 0U) {
		if (value >= result + bit) {
			value -= result + bit;
			result = (result >> 1) + bit;
		} else {
			result >>= 1;
		}
		bit >>= 2;
	}
	return result;
}

static uint32_t magnitude_mg(int32_t x, int32_t y, int32_t z)
{
	uint32_t sum = (uint32_t)(x * x + y * y + z * z);
	return isqrt_u32(sum);
}

#if defined(CONFIG_BOARD_THINKNODE_M3)
static int accel_init(void)
{
	uint8_t whoami = 0;
	int rc;

	if (!device_is_ready(accel_power)) {
		return -ENODEV;
	}
	rc = regulator_enable(accel_power);
	if (rc != 0) {
		return rc;
	}
	k_sleep(K_MSEC(10));
	rc = i2c_reg_read_byte(i2c, I2C_ADDR_SC7A20, 0x0F, &whoami);
	if (rc != 0 || whoami != 0x11) {
		LOG_WRN("SC7A20H not found (rc=%d id=0x%02x)", rc, whoami);
		return -ENODEV;
	}
	/* 100 Hz, XYZ enabled; BDU + high resolution + +/-8 g. */
	rc = i2c_reg_write_byte(i2c, I2C_ADDR_SC7A20, 0x20, 0x57);
	if (rc == 0) {
		rc = i2c_reg_write_byte(i2c, I2C_ADDR_SC7A20, 0x23, 0xA8);
	}
	return rc;
}

static int accel_read_mg(int32_t *x, int32_t *y, int32_t *z)
{
	uint8_t data[6];
	int rc = i2c_burst_read(i2c, I2C_ADDR_SC7A20, 0x28 | BIT(7), data, sizeof(data));
	if (rc != 0) {
		return rc;
	}
	/* 12-bit left-aligned, high resolution, +/-8 g = 4 mg/LSB. */
	*x = ((int16_t)((uint16_t)data[1] << 8 | data[0]) >> 4) * 4;
	*y = ((int16_t)((uint16_t)data[3] << 8 | data[2]) >> 4) * 4;
	*z = ((int16_t)((uint16_t)data[5] << 8 | data[4]) >> 4) * 4;
	return 0;
}
#else
static int accel_init(void)
{
	uint8_t whoami = 0;
	int rc = i2c_reg_read_byte(i2c, I2C_ADDR_QMA6100, 0x00, &whoami);

	if (rc != 0 || whoami != 0x90) {
		LOG_WRN("QMA6100P not found (rc=%d id=0x%02x)", rc, whoami);
		return -ENODEV;
	}
	/* +/-8 g, 100 Hz, active mode. */
	rc = i2c_reg_write_byte(i2c, I2C_ADDR_QMA6100, 0x0F, 0x04);
	if (rc == 0) {
		rc = i2c_reg_write_byte(i2c, I2C_ADDR_QMA6100, 0x10, 0x05);
	}
	if (rc == 0) {
		rc = i2c_reg_write_byte(i2c, I2C_ADDR_QMA6100, 0x11, 0x85);
	}
	return rc;
}

static int accel_read_mg(int32_t *x, int32_t *y, int32_t *z)
{
	uint8_t data[6];
	int rc = i2c_burst_read(i2c, I2C_ADDR_QMA6100, 0x01, data, sizeof(data));
	if (rc != 0) {
		return rc;
	}
	/* QMA6100P samples are signed 14-bit, left aligned by two bits.
	 * In +/-8 g mode one g is 1024 LSB. */
	int16_t raw_x = (int16_t)(((uint16_t)data[1] << 8 | data[0]) >> 2);
	int16_t raw_y = (int16_t)(((uint16_t)data[3] << 8 | data[2]) >> 2);
	int16_t raw_z = (int16_t)(((uint16_t)data[5] << 8 | data[4]) >> 2);
	if (raw_x & BIT(13)) raw_x |= (int16_t)~0x3FFF;
	if (raw_y & BIT(13)) raw_y |= (int16_t)~0x3FFF;
	if (raw_z & BIT(13)) raw_z |= (int16_t)~0x3FFF;
	*x = (int32_t)raw_x * 1000 / 1024;
	*y = (int32_t)raw_y * 1000 / 1024;
	*z = (int32_t)raw_z * 1000 / 1024;
	return 0;
}
#endif

static void detector_reset(void)
{
	state = DETECTOR_IDLE;
	still_samples = 0;
	settle_samples = 0;
}

static void detector_sample(struct k_work *work)
{
	ARG_UNUSED(work);
	int32_t x, y, z;
	uint32_t now = k_uptime_get_32();

	if (accel_read_mg(&x, &y, &z) == 0) {
		uint32_t magnitude = magnitude_mg(x, y, z);
		switch (state) {
		case DETECTOR_IDLE:
			if (magnitude <= FREE_FALL_MAX_MG) {
				state = DETECTOR_FREE_FALL;
				state_started_ms = now;
			}
			break;
		case DETECTOR_FREE_FALL:
			if (magnitude >= IMPACT_MIN_MG) {
				state = DETECTOR_SETTLING;
				state_started_ms = now;
				still_samples = 0;
				settle_samples = 0;
			} else if (now - state_started_ms > FREE_FALL_WINDOW_MS) {
				detector_reset();
			}
			break;
		case DETECTOR_SETTLING:
			if (now - state_started_ms >= SETTLE_START_MS) {
				settle_samples++;
				int32_t deviation = (int32_t)magnitude - 1000;
				if (deviation < 0) {
					deviation = -deviation;
				}
				if ((uint32_t)deviation <= STILL_BAND_MG) {
					still_samples++;
				}
			}
			if (now - state_started_ms >= SETTLE_WINDOW_MS) {
				if (settle_samples != 0 && still_samples * 100U >= settle_samples * 85U) {
					LOG_WRN("fall detected");
					state = DETECTOR_COOLDOWN;
					state_started_ms = now;
					if (detected_callback) {
						detected_callback();
					}
				} else {
					detector_reset();
				}
			}
			break;
		case DETECTOR_COOLDOWN:
			if (now - state_started_ms >= FALL_COOLDOWN_MS) {
				detector_reset();
			}
			break;
		}
	}

	k_work_schedule(&sample_work, K_MSEC(SAMPLE_PERIOD_MS));
}

} // namespace

extern "C" void fall_detector_begin(fall_detector_callback_t callback)
{
	detected_callback = callback;
	if (!device_is_ready(i2c)) {
		LOG_WRN("fall detector disabled: I2C unavailable");
		return;
	}
	if (accel_init() != 0) {
		LOG_WRN("fall detector disabled: accelerometer unavailable");
		return;
	}
	available = true;
	detector_reset();
	k_work_init_delayable(&sample_work, detector_sample);
	k_work_schedule(&sample_work, K_MSEC(SAMPLE_PERIOD_MS));
	LOG_INF("fall detector enabled");
}

extern "C" bool fall_detector_is_available(void)
{
	return available;
}
