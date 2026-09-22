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
	uint8_t free_fall_min_samples;
	uint16_t free_fall_window_ms;
	uint16_t settle_start_ms;
	uint16_t settle_window_ms;
	uint8_t settle_stable_min_percent;
	uint16_t settle_vector_delta_mg;
	uint16_t posture_change_min_mg;
};

/* Level 3 is the original, proven M3 profile.  The last two values are used
 * only by QMA6100P, whose fixed board offset makes magnitude-only stillness
 * too permissive. */
constexpr fall_profile FALL_PROFILES[] = {
	{ 850, 1800, 1, 1000,  600, 6000, 80, 0, 0 }, /* 1: most sensitive */
	{ 750, 2300, 1,  800,  800, 7000, 82, 0, 0 }, /* 2 */
	{ 650, 2800, 1,  600, 1000, 8000, 85, 0, 0 }, /* 3: M3 default */
	{ 550, 3300, 1,  500, 1000, 8000, 88, 0, 0 }, /* 4 */
	{ 450, 3800, 1,  450, 1000, 8000, 90, 0, 0 }, /* 5: strict */
};
#if defined(CONFIG_BOARD_T1000_E)
/* A device carried in a backpack is routinely shaken and bumped.  A bump can
 * make both a low-g sample and a peak, but it normally has no sustained change
 * in resting orientation.  Keep the fall sequence short (low-g -> impact) and
 * require its final resting vector to differ substantially from the vector
 * before the event. */
constexpr fall_profile T1000_FALL_PROFILES[] = {
	/* QMA6100P samples from a real 1--1.5 m fall: low-g about 790 mg,
	 * impact about 1.43--1.50 g, post-fall posture delta about 536 mg. */
	{ 850, 1150, 1, 450,  700,  7000, 85, 800, 250 }, /* 1: most sensitive */
	{ 825, 1200, 1, 400,  800,  7500, 86, 750, 350 }, /* 2 */
	{ 800, 1250, 1, 350, 1000,  8000, 88, 650, 450 }, /* 3: default */
	{ 700, 1550, 2, 300, 1200,  8500, 90, 450, 600 }, /* 4 */
	{ 600, 1900, 3, 250, 1400, 10000, 92, 300, 800 }, /* 5: strict */
};
#endif
constexpr uint32_t SAMPLE_PERIOD_MS = 20;
/* Stillness is relative, not tied to an ideal 1 g magnitude: real QMA6100P
 * offsets vary by board and mounting orientation. */
constexpr uint32_t SETTLE_REFERENCE_MIN_MG = 200;
constexpr uint32_t SETTLE_REFERENCE_MAX_MG = 3000;
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
static fall_detector_callback_t prealert_callback;
static enum detector_state state;
static uint32_t state_started_ms;
static uint32_t still_samples;
static uint32_t settle_samples;
static uint32_t settle_reference_mg;
static uint8_t free_fall_samples;
static int32_t idle_x;
static int32_t idle_y;
static int32_t idle_z;
static uint8_t idle_samples;
static int32_t pre_fall_x;
static int32_t pre_fall_y;
static int32_t pre_fall_z;
static int32_t settle_x;
static int32_t settle_y;
static int32_t settle_z;
static uint8_t consecutive_read_errors;
static uint8_t sensitivity = 3;
static bool available;
static bool enabled = true;
static bool sample_work_initialized;
static bool accel_power_enabled;

static const fall_profile &active_profile(void)
{
#if defined(CONFIG_BOARD_T1000_E)
	return T1000_FALL_PROFILES[sensitivity - 1U];
#else
	return FALL_PROFILES[sensitivity - 1U];
#endif
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

static uint32_t vector_delta_mg(int32_t ax, int32_t ay, int32_t az,
				int32_t bx, int32_t by, int32_t bz)
{
	return magnitude_mg(ax - bx, ay - by, az - bz);
}

static void update_idle_reference(int32_t x, int32_t y, int32_t z, uint32_t magnitude)
{
#if defined(CONFIG_BOARD_T1000_E)
	if (magnitude < SETTLE_REFERENCE_MIN_MG || magnitude > SETTLE_REFERENCE_MAX_MG) {
		return;
	}
	if (idle_samples == 0) {
		idle_x = x;
		idle_y = y;
		idle_z = z;
	} else {
		/* Slow EWMA: motion before the suspected fall cannot rewrite the
		 * reference in one or two samples. */
		idle_x += (x - idle_x) / 16;
		idle_y += (y - idle_y) / 16;
		idle_z += (z - idle_z) / 16;
	}
	if (idle_samples < UINT8_MAX) {
		idle_samples++;
	}
#else
	ARG_UNUSED(x);
	ARG_UNUSED(y);
	ARG_UNUSED(z);
	ARG_UNUSED(magnitude);
#endif
}

static int accel_power_on(void)
{
	if (accel_power_enabled) {
		return 0;
	}
	if (!device_is_ready(accel_power)) {
		return -ENODEV;
	}
	int rc = regulator_enable(accel_power);
	if (rc == 0) {
		accel_power_enabled = true;
	}
	return rc;
}

static void accel_power_off(void)
{
	if (!accel_power_enabled) {
		return;
	}
	(void)regulator_disable(accel_power);
	accel_power_enabled = false;
}

#if defined(CONFIG_BOARD_THINKNODE_M3)
static int accel_init(void)
{
	uint8_t whoami = 0;
	int rc;

	rc = accel_power_on();
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
	if (!device_is_ready(t1000_sensor_power) ||
	    !device_is_ready(t1000_sensor_enable)) {
		return -ENODEV;
	}
	rc = regulator_enable(t1000_sensor_power);
	if (rc != 0) {
		return rc;
	}
	rc = accel_power_on();
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

	/* Preserve the board's known-good clock and filter setup.  Explicitly
	 * replacing PM/ODR changed the measured fall waveform on real hardware. */
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
	free_fall_samples = 0;
	still_samples = 0;
	settle_samples = 0;
	settle_reference_mg = 0;
	settle_x = 0;
	settle_y = 0;
	settle_z = 0;
}

static void detector_sample(struct k_work *work)
{
	ARG_UNUSED(work);
	int32_t x, y, z;
	uint32_t now = k_uptime_get_32();
	const fall_profile &profile = active_profile();
	if (!enabled) {
		return;
	}

	if (accel_read_mg(&x, &y, &z) == 0) {
		consecutive_read_errors = 0;
		uint32_t magnitude = magnitude_mg(x, y, z);
		switch (state) {
		case DETECTOR_IDLE: {
#if defined(CONFIG_BOARD_T1000_E)
			bool low_g = magnitude <= profile.free_fall_max_mg;
			if (!low_g) {
				update_idle_reference(x, y, z, magnitude);
			}
#else
			bool low_g = magnitude <= profile.free_fall_max_mg;
#endif
			if (low_g) {
				if (free_fall_samples == 0) {
					state_started_ms = now;
				}
				if (free_fall_samples < UINT8_MAX) {
					free_fall_samples++;
				}
				if (free_fall_samples >= profile.free_fall_min_samples) {
#if defined(CONFIG_BOARD_T1000_E)
					pre_fall_x = idle_x;
					pre_fall_y = idle_y;
					pre_fall_z = idle_z;
#endif
					state = DETECTOR_FREE_FALL;
				}
			} else {
				free_fall_samples = 0;
#if !defined(CONFIG_BOARD_T1000_E)
				update_idle_reference(x, y, z, magnitude);
#endif
			}
			break;
		}
		case DETECTOR_FREE_FALL:
			if (magnitude >= profile.impact_min_mg) {
				state = DETECTOR_SETTLING;
				state_started_ms = now;
				still_samples = 0;
				settle_samples = 0;
				settle_reference_mg = 0;
				settle_x = 0;
				settle_y = 0;
				settle_z = 0;
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
						settle_x = x;
						settle_y = y;
						settle_z = z;
						still_samples++;
					}
				} else {
					uint32_t delta = vector_delta_mg(x, y, z,
						settle_x, settle_y, settle_z);
					if (delta <= profile.settle_vector_delta_mg) {
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
				uint32_t posture_delta = vector_delta_mg(settle_x, settle_y, settle_z,
					pre_fall_x, pre_fall_y, pre_fall_z);
				if (settle_reference_mg != 0 &&
					posture_delta >= profile.posture_change_min_mg &&
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
					if (prealert_callback) {
						prealert_callback();
					}
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
	k_work_init_delayable(&sample_work, detector_sample);
	sample_work_initialized = true;
	if (!enabled) {
		available = false;
		LOG_INF("fall detector disabled");
		return;
	}
	if (!device_is_ready(i2c)) {
		LOG_WRN("fall detector disabled: I2C unavailable");
		return;
	}
	if (accel_init() != 0) {
		LOG_WRN("fall detector disabled: accelerometer unavailable");
		accel_power_off();
		return;
	}
	available = true;
	detector_reset();
	k_work_schedule(&sample_work, K_MSEC(SAMPLE_PERIOD_MS));
	LOG_INF("fall detector enabled");
}

extern "C" void fall_detector_set_prealert_callback(fall_detector_callback_t callback)
{
	prealert_callback = callback;
}

extern "C" bool fall_detector_is_available(void)
{
	return available;
}

extern "C" bool fall_detector_set_enabled(bool value)
{
	enabled = value;
	detector_reset();
	consecutive_read_errors = 0;
	if (!sample_work_initialized) {
		return true;
	}
	if (!value) {
		(void)k_work_cancel_delayable(&sample_work);
		accel_power_off();
		available = false;
		return true;
	}
	if (accel_init() != 0) {
		available = false;
		accel_power_off();
		return false;
	}
	available = true;
	k_work_schedule(&sample_work, K_MSEC(SAMPLE_PERIOD_MS));
	return true;
}

extern "C" bool fall_detector_is_enabled(void)
{
	return enabled;
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
