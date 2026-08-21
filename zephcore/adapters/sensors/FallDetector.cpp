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
struct fall_profile {
	uint16_t free_fall_max_mg;
	uint16_t impact_min_mg;
	uint16_t free_fall_window_ms;
	uint16_t settle_start_ms;
	uint16_t settle_window_ms;
	uint8_t settle_stable_min_percent;
};

/* Level 3 is the original, proven M3 profile. */
constexpr fall_profile FALL_PROFILES[] = {
	{ 450, 3800,  450, 1000, 8000, 90 }, /* 1: strict */
	{ 550, 3300,  500, 1000, 8000, 88 }, /* 2 */
	{ 650, 2800,  600, 1000, 8000, 85 }, /* 3: M3 default */
	{ 750, 2300,  800,  800, 7000, 82 }, /* 4 */
	{ 850, 1800, 1000,  600, 6000, 80 }, /* 5: most sensitive */
};
constexpr uint32_t SAMPLE_PERIOD_MS = 20;
/* Stillness is relative, not tied to an ideal 1 g magnitude: real QMA6100P
 * offsets vary by board and mounting orientation. */
constexpr uint32_t SETTLE_REFERENCE_MIN_MG = 200;
constexpr uint32_t SETTLE_REFERENCE_MAX_MG = 3000;
constexpr uint32_t SETTLE_STABLE_DELTA_MG = 500;
constexpr uint32_t FALL_COOLDOWN_MS = 60000;

enum detector_state {
	DETECTOR_IDLE,
	DETECTOR_FREE_FALL,
	DETECTOR_SETTLING,
	DETECTOR_COOLDOWN,
};

static const struct device *const i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));
#if defined(CONFIG_BOARD_THINKNODE_M3) || defined(CONFIG_BOARD_T1000_E)
static const struct device *const accel_power = DEVICE_DT_GET(DT_ALIAS(accel_power));
#endif
#if defined(CONFIG_BOARD_T1000_E)
static const struct device *const t1000_sensor_power =
	DEVICE_DT_GET(DT_NODELABEL(sensor_power));
static const struct device *const t1000_sensor_enable =
	DEVICE_DT_GET(DT_NODELABEL(t1000_sensor_enable));
#endif

static struct k_work_delayable sample_work;
static fall_detector_callback_t detected_callback;
static enum detector_state state;
static uint32_t state_started_ms;
static uint32_t still_samples;
static uint32_t settle_samples;
static uint32_t settle_reference_mg;
static uint8_t consecutive_read_errors;
static uint8_t sensitivity = 3;
static bool available;

static const fall_profile &active_profile(void)
{
	return FALL_PROFILES[sensitivity - 1U];
}

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
static int qma6100_update_reg(uint8_t reg, uint8_t mask, uint8_t value)
{
	uint8_t current;
	int rc = i2c_reg_read_byte(i2c, I2C_ADDR_QMA6100, reg, &current);

	if (rc != 0) {
		return rc;
	}
	return i2c_reg_write_byte(i2c, I2C_ADDR_QMA6100, reg,
		(current & ~mask) | (value & mask));
}

static int accel_init(void)
{
	uint8_t whoami = 0;
	int rc;

	/* T1000-E powers its I2C rail from P1.6 and the QMA6100P itself from
	 * P1.7.  This order and 20ms settling time match the board reference
	 * firmware. P0.4 is kept high too: it is part of the shared sensor rail. */
	if (!device_is_ready(t1000_sensor_power) || !device_is_ready(accel_power) ||
	    !device_is_ready(t1000_sensor_enable)) {
		return -ENODEV;
	}
	rc = regulator_enable(t1000_sensor_power);
	if (rc != 0) {
		return rc;
	}
	rc = regulator_enable(accel_power);
	if (rc != 0) {
		return rc;
	}
	rc = regulator_enable(t1000_sensor_enable);
	if (rc != 0) {
		return rc;
	}
	k_sleep(K_MSEC(20));
	/* The T1000-E QMA6100P can leave SDA low while its rail comes up.  The
	 * nRF TWIM recovery generates the same SCL recovery clocks used by the
	 * reference firmware before probing the sensor. */
	rc = i2c_recover_bus(i2c);
	if (rc != 0) {
		LOG_WRN("QMA6100P I2C recovery failed: %d", rc);
	}
	rc = i2c_reg_read_byte(i2c, I2C_ADDR_QMA6100, 0x00, &whoami);

	if (rc != 0 || whoami != 0x90) {
		LOG_WRN("QMA6100P not found (rc=%d id=0x%02x)", rc, whoami);
		return -ENODEV;
	}

	/* QMA6100P reference sequence: reset, select +/-8g, then enter active
	 * mode.  Do not overwrite the clock and filter bits left by the reset. */
	rc = i2c_reg_write_byte(i2c, I2C_ADDR_QMA6100, 0x36, 0xB6);
	if (rc == 0) {
		k_sleep(K_MSEC(5));
		rc = i2c_reg_write_byte(i2c, I2C_ADDR_QMA6100, 0x36, 0x00);
	}
	if (rc == 0) {
		k_sleep(K_MSEC(20));
		rc = qma6100_update_reg(0x0F, 0x0F, 0x04); /* +/-8g */
	}
	if (rc == 0) {
		rc = qma6100_update_reg(0x11, BIT(7), BIT(7)); /* active */
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
	settle_reference_mg = 0;
}

static void detector_sample(struct k_work *work)
{
	ARG_UNUSED(work);
	int32_t x, y, z;
	uint32_t now = k_uptime_get_32();
	const fall_profile &profile = active_profile();

	if (accel_read_mg(&x, &y, &z) == 0) {
		consecutive_read_errors = 0;
		uint32_t magnitude = magnitude_mg(x, y, z);
		switch (state) {
		case DETECTOR_IDLE:
			if (magnitude <= profile.free_fall_max_mg) {
				state = DETECTOR_FREE_FALL;
				state_started_ms = now;
			}
			break;
		case DETECTOR_FREE_FALL:
			if (magnitude >= profile.impact_min_mg) {
				state = DETECTOR_SETTLING;
				state_started_ms = now;
				still_samples = 0;
				settle_samples = 0;
				settle_reference_mg = 0;
			} else if (now - state_started_ms > profile.free_fall_window_ms) {
				detector_reset();
			}
			break;
		case DETECTOR_SETTLING:
			if (now - state_started_ms >= profile.settle_start_ms) {
				settle_samples++;
#if defined(CONFIG_BOARD_T1000_E)
				/* QMA6100P may occasionally return a single implausible sample
				 * while the device is already still.  Establish a normal resting
				 * reference, then count only readings close to it. */
				if (settle_reference_mg == 0) {
					if (magnitude >= SETTLE_REFERENCE_MIN_MG &&
					    magnitude <= SETTLE_REFERENCE_MAX_MG) {
						settle_reference_mg = magnitude;
						still_samples++;
					}
				} else {
					uint32_t delta = magnitude > settle_reference_mg ?
						magnitude - settle_reference_mg :
						settle_reference_mg - magnitude;
					if (delta <= SETTLE_STABLE_DELTA_MG) {
						still_samples++;
					}
				}
#else
				int32_t deviation = (int32_t)magnitude - 1000;
				if (deviation < 0) {
					deviation = -deviation;
				}
				if ((uint32_t)deviation <= 200U) {
					still_samples++;
				}
#endif
			}
			if (now - state_started_ms >= profile.settle_window_ms) {
#if defined(CONFIG_BOARD_T1000_E)
				uint32_t still_pct = settle_samples != 0 ?
					(still_samples * 100U) / settle_samples : 0U;
				if (settle_reference_mg != 0 &&
					still_pct >= profile.settle_stable_min_percent) {
#else
				uint32_t still_pct = settle_samples != 0 ?
					(still_samples * 100U) / settle_samples : 0U;
				if (settle_samples != 0 &&
					still_pct >= profile.settle_stable_min_percent) {
#endif
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
	} else if (++consecutive_read_errors >= 5) {
		/* P1.6 is shared with the ADC front-end.  A transient power or bus
		 * failure must not leave fall detection silently dead. */
		LOG_WRN("accelerometer read failed; reinitializing");
		consecutive_read_errors = 0;
		detector_reset();
		if (accel_init() != 0) {
			available = false;
		} else {
			available = true;
		}
	}

	k_work_schedule(&sample_work, K_MSEC(SAMPLE_PERIOD_MS));
}

} // namespace

extern "C" void fall_detector_begin(fall_detector_callback_t callback)
{
	detected_callback = callback;
	consecutive_read_errors = 0;
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

extern "C" bool fall_detector_set_sensitivity(uint8_t value)
{
	if (value < 1U || value > ARRAY_SIZE(FALL_PROFILES)) {
		return false;
	}
	sensitivity = value;
	detector_reset();
	return true;
}

extern "C" uint8_t fall_detector_get_sensitivity(void)
{
	return sensitivity;
}

extern "C" void fall_detector_reset(void)
{
	detector_reset();
	consecutive_read_errors = 0;
}
