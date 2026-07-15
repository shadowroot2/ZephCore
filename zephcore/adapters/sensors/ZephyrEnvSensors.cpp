/*
 * SPDX-License-Identifier: MIT
 * Zephyr Environment & Power Sensors
 *
 * Auto-detects available sensors via Zephyr devicetree nodelabels.
 *
 * Environment sensors (temp/humidity/pressure):
 *   SHTC3, AHT20/DHT20/AM2301B, SHT4x, SHT3xD, BME280, BME680, BMP280, BMP388, LPS22HB
 *   MCU die temperature as fallback when no external sensor is present
 *
 * Power monitors (voltage/current/power):
 *   INA219, INA3221, INA226, INA228, INA230, INA232, INA236, INA237
 */

#include "ZephyrEnvSensors.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#if IS_ENABLED(CONFIG_ADC)
#include <zephyr/drivers/adc.h>
#endif

#if IS_ENABLED(CONFIG_REGULATOR)
#include <zephyr/drivers/regulator.h>
#endif

LOG_MODULE_REGISTER(zephcore_sensors, CONFIG_ZEPHCORE_SENSORS_LOG_LEVEL);

/* ========== Sensor Support ========== */
#if IS_ENABLED(CONFIG_SENSOR)
#define HAS_ENV_SENSORS 1
#include <zephyr/drivers/sensor.h>
#else
#define HAS_ENV_SENSORS 0
#endif

#if HAS_ENV_SENSORS && IS_ENABLED(CONFIG_ADC) && IS_ENABLED(CONFIG_REGULATOR) && \
	DT_NODE_EXISTS(DT_NODELABEL(t1000_light_adc))
#define HAS_T1000_ANALOG_LIGHT 1
#else
#define HAS_T1000_ANALOG_LIGHT 0
#endif

#if HAS_ENV_SENSORS && (IS_ENABLED(CONFIG_TEMP_NRF5) || IS_ENABLED(CONFIG_TEMP_NRFS)) && \
	DT_NODE_EXISTS(DT_NODELABEL(temp))
#define MCU_TEMP_NODE DT_NODELABEL(temp)
#elif HAS_ENV_SENSORS && IS_ENABLED(CONFIG_ESP32_TEMP) && DT_NODE_EXISTS(DT_NODELABEL(coretemp))
#define MCU_TEMP_NODE DT_NODELABEL(coretemp)
#endif

/* INA3221 channel selection attribute — defined in driver's private header
 * (zephyr/drivers/sensor/ti/ina3221/ina3221.h), replicated here to avoid
 * including private driver internals */
#define SENSOR_ATTR_INA3221_SELECTED_CHANNEL (SENSOR_ATTR_PRIV_START + 1)

/* ================================================================
 *  DEVICE_DT_GET_OR_NULL helper
 *
 *  Returns a const struct device* for a DT nodelabel if it exists
 *  in the board's devicetree, or NULL at compile time if it doesn't.
 *  This is the proper Zephyr pattern — device_get_binding() is deprecated.
 * ================================================================ */

/* ================================================================
 *  Environment Sensors
 * ================================================================ */

#if HAS_ENV_SENSORS
static const struct device *temp_humidity_dev = NULL;
static const struct device *pressure_dev = NULL;
static bool temp_dev_has_pressure = false;  /* BME280/BME680 also have pressure */
static bool env_available = false;
#endif

#if HAS_T1000_ANALOG_LIGHT
static const struct device *t1000_light_adc_dev =
	DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(t1000_light_adc)));
static const struct adc_channel_cfg t1000_light_adc_cfg =
	ADC_CHANNEL_CFG_DT(DT_NODELABEL(t1000_light_adc));
static const uint8_t t1000_light_adc_channel =
	DT_REG_ADDR(DT_NODELABEL(t1000_light_adc));
static const uint8_t t1000_light_adc_resolution =
	DT_PROP(DT_NODELABEL(t1000_light_adc), zephyr_resolution);

#if IS_ENABLED(CONFIG_REGULATOR) && DT_NODE_EXISTS(DT_NODELABEL(sensor_power))
static const struct device *t1000_sensor_power =
	DEVICE_DT_GET_OR_NULL(DT_NODELABEL(sensor_power));
#else
static const struct device *t1000_sensor_power = NULL;
#endif

#if IS_ENABLED(CONFIG_REGULATOR) && DT_NODE_EXISTS(DT_NODELABEL(t1000_sensor_enable))
static const struct device *t1000_sensor_enable =
	DEVICE_DT_GET_OR_NULL(DT_NODELABEL(t1000_sensor_enable));
#else
static const struct device *t1000_sensor_enable = NULL;
#endif

static bool t1000_light_available = false;

static bool regulator_ready(const struct device *dev)
{
	return dev && device_is_ready(dev);
}

static int t1000_enable_analog_power(void)
{
	int rc;

	rc = regulator_enable(t1000_sensor_power);
	if (rc < 0) {
		return rc;
	}

	rc = regulator_enable(t1000_sensor_enable);
	if (rc < 0) {
		regulator_disable(t1000_sensor_power);
		return rc;
	}

	k_sleep(K_MSEC(10));
	return 0;
}

static void t1000_disable_analog_power(void)
{
	regulator_disable(t1000_sensor_enable);
	regulator_disable(t1000_sensor_power);
}

static int t1000_adc_read_average_raw(int32_t *raw_avg)
{
	const int samples = 15;
	int32_t sum = 0;

	for (int i = 0; i < samples; ++i) {
		int16_t sample = 0;
		struct adc_sequence sequence;
		int rc;

		sequence = {};
		sequence.channels = BIT(t1000_light_adc_channel);
		sequence.buffer = &sample;
		sequence.buffer_size = sizeof(sample);
		sequence.resolution = t1000_light_adc_resolution;

		rc = adc_read(t1000_light_adc_dev, &sequence);
		if (rc < 0) {
			return rc;
		}

		sum += sample;
	}

	*raw_avg = sum / samples;
	return 0;
}

static float t1000_light_level_from_raw(int32_t raw)
{
	if (raw < 0) {
		raw = 0;
	}

	uint32_t mv = ((uint32_t)raw * 3000U + 2048U) / 4096U;

	if (mv <= 80U) {
		return 0.0f;
	}
	if (mv >= 2480U) {
		return 100.0f;
	}

	return (100.0f * (float)(mv - 80U)) / 2400.0f;
}

static void t1000_analog_light_init(void)
{
	if (!device_is_ready(t1000_light_adc_dev)) {
		LOG_WRN("T1000-E light ADC is not ready");
		return;
	}

	if (!regulator_ready(t1000_sensor_power) || !regulator_ready(t1000_sensor_enable)) {
		LOG_WRN("T1000-E analog sensor regulators are not ready");
		return;
	}

	int rc = adc_channel_setup(t1000_light_adc_dev, &t1000_light_adc_cfg);
	if (rc < 0) {
		LOG_WRN("T1000-E light ADC setup failed: %d", rc);
		return;
	}

	t1000_light_available = true;
	LOG_INF("Found T1000-E analog light sensor");
}

static bool t1000_read_light(struct env_data *data)
{
	int32_t raw = 0;
	int rc;

	if (!t1000_light_available) {
		return false;
	}

	rc = t1000_enable_analog_power();
	if (rc < 0) {
		LOG_WRN("T1000-E analog sensor power enable failed: %d", rc);
		return false;
	}

	rc = t1000_adc_read_average_raw(&raw);
	t1000_disable_analog_power();
	if (rc < 0) {
		LOG_WRN("T1000-E light ADC read failed: %d", rc);
		return false;
	}

	data->light_lux = t1000_light_level_from_raw(raw);
	data->has_light = true;
	return true;
}
#endif

int env_sensors_init(void)
{
#if HAS_ENV_SENSORS
	const struct device *dev;

	/* === Temperature/Humidity sensors ===
	 * Priority order: dedicated temp/humidity first, then combo sensors.
	 * BME280/BME680 also provide pressure — tracked via temp_dev_has_pressure. */

	/* SHTC3 (e.g., RAK1901) */
	dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(shtc3));
	if (dev && device_is_ready(dev)) {
		temp_humidity_dev = dev;
		LOG_INF("Found temp/humidity sensor: %s (SHTC3)", dev->name);
		goto check_pressure;
	}

	/* Aosong AHT20/DHT20/AM2301B — same chip family, three compatible strings */
	dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(aht20));
	if (!dev || !device_is_ready(dev)) {
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(dht20));
	}
	if (!dev || !device_is_ready(dev)) {
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(am2301b));
	}
	if (dev && device_is_ready(dev)) {
		temp_humidity_dev = dev;
		LOG_INF("Found temp/humidity sensor: %s (AHT20/DHT20)", dev->name);
		goto check_pressure;
	}

	/* SHT4x */
	dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(sht4x));
	if (dev && device_is_ready(dev)) {
		temp_humidity_dev = dev;
		LOG_INF("Found temp/humidity sensor: %s (SHT4x)", dev->name);
		goto check_pressure;
	}

	/* SHT3xD */
	dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(sht3xd));
	if (dev && device_is_ready(dev)) {
		temp_humidity_dev = dev;
		LOG_INF("Found temp/humidity sensor: %s (SHT3xD)", dev->name);
		goto check_pressure;
	}

	/* BME280 — temperature + humidity + pressure */
	dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(bme280));
	if (dev && device_is_ready(dev)) {
		temp_humidity_dev = dev;
		temp_dev_has_pressure = true;
		LOG_INF("Found env sensor: %s (BME280 — temp/humidity/pressure)", dev->name);
		goto check_pressure;
	}

	/* BME680 — temperature + humidity + pressure (+ gas) */
	dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(bme680));
	if (dev && device_is_ready(dev)) {
		temp_humidity_dev = dev;
		temp_dev_has_pressure = true;
		LOG_INF("Found env sensor: %s (BME680 — temp/humidity/pressure)", dev->name);
		goto check_pressure;
	}

check_pressure:
	/* === Pressure-only sensors ===
	 * Only needed if the temp/humidity sensor doesn't provide pressure. */
	if (!temp_dev_has_pressure) {
		/* LPS22HB (e.g., RAK1902) */
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(lps22hb));
		if (dev && device_is_ready(dev)) {
			pressure_dev = dev;
			LOG_INF("Found pressure sensor: %s (LPS22HB)", dev->name);
			goto done;
		}

		/* BMP280 — pressure + temperature (lower priority as temp source) */
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(bmp280));
		if (dev && device_is_ready(dev)) {
			pressure_dev = dev;
			LOG_INF("Found pressure sensor: %s (BMP280)", dev->name);
			goto done;
		}

		/* BMP388 */
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(bmp388));
		if (dev && device_is_ready(dev)) {
			pressure_dev = dev;
			LOG_INF("Found pressure sensor: %s (BMP388)", dev->name);
			goto done;
		}
	}

done:
	env_available = (temp_humidity_dev != NULL) || (pressure_dev != NULL);
#if HAS_T1000_ANALOG_LIGHT
	t1000_analog_light_init();
	env_available = env_available || t1000_light_available;
#endif
	if (!env_available) {
		LOG_INF("No environment sensors found");
	}
#endif

	return 0;
}

bool env_sensors_available(void)
{
#if HAS_ENV_SENSORS
	return env_available;
#else
	return false;
#endif
}

int env_sensors_read(struct env_data *data)
{
	memset(data, 0, sizeof(*data));

#if HAS_ENV_SENSORS
	struct sensor_value val;
	int rc;

	/* === Read temperature/humidity sensor === */
	if (temp_humidity_dev) {
		rc = sensor_sample_fetch(temp_humidity_dev);
		if (rc == 0) {
			if (sensor_channel_get(temp_humidity_dev, SENSOR_CHAN_AMBIENT_TEMP, &val) == 0) {
				data->temperature_c = sensor_value_to_float(&val);
				data->has_temperature = true;
			}
			if (sensor_channel_get(temp_humidity_dev, SENSOR_CHAN_HUMIDITY, &val) == 0) {
				data->humidity_pct = sensor_value_to_float(&val);
				data->has_humidity = true;
			}
			/* BME280/BME680 also have pressure — read from same device */
			if (temp_dev_has_pressure) {
				if (sensor_channel_get(temp_humidity_dev, SENSOR_CHAN_PRESS, &val) == 0) {
					data->pressure_hpa = sensor_value_to_float(&val) * 10.0f;
					data->has_pressure = true;
				}
			}
		}
	}

	/* === Read dedicated pressure sensor (if not already from combo sensor) === */
	if (pressure_dev && !data->has_pressure) {
		if (pressure_dev != temp_humidity_dev) {
			sensor_sample_fetch(pressure_dev);
		}
		if (sensor_channel_get(pressure_dev, SENSOR_CHAN_PRESS, &val) == 0) {
			data->pressure_hpa = sensor_value_to_float(&val) * 10.0f;
			data->has_pressure = true;
		}
	}

	/* === MCU die temperature — fallback when no external temp sensor exists. */
#ifdef MCU_TEMP_NODE
	const struct device *mcu_temp = DEVICE_DT_GET(MCU_TEMP_NODE);
	if (mcu_temp && device_is_ready(mcu_temp)) {
		if (sensor_sample_fetch(mcu_temp) == 0 &&
		    sensor_channel_get(mcu_temp, SENSOR_CHAN_DIE_TEMP, &val) == 0) {
			data->mcu_temperature_c = sensor_value_to_float(&val);
			data->has_mcu_temperature = true;
		}
	}
#endif

#if HAS_T1000_ANALOG_LIGHT
	t1000_read_light(data);
#endif

	return (data->has_temperature || data->has_humidity || data->has_pressure ||
	        data->has_light || data->has_mcu_temperature) ? 0 : -ENODATA;
#else
	return -ENOTSUP;
#endif
}

/* ================================================================
 *  Power Monitor Sensors (INA family)
 *
 *  Three Zephyr driver families:
 *  - INA219  (ti,ina219)  — standalone, 1 channel
 *  - INA3221 (ti,ina3221) — standalone, 3 channels with channel selection
 *  - ina2xx  (ti,ina226/228/230/232/236/237) — unified driver, 1 channel
 *
 *  All use: SENSOR_CHAN_VOLTAGE, SENSOR_CHAN_CURRENT, SENSOR_CHAN_POWER
 * ================================================================ */

#if HAS_ENV_SENSORS

enum ina_type { INA_NONE, INA_219, INA_3221, INA_2XX };

static const struct device *ina_dev = NULL;
static enum ina_type ina_found = INA_NONE;
static uint8_t ina_num_channels = 0;
static bool ina_available = false;

#endif /* HAS_ENV_SENSORS */

int power_sensors_init(void)
{
#if HAS_ENV_SENSORS
	const struct device *dev;

	/* INA3221 — 3-channel power monitor (check first — most channels) */
	dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(ina3221));
	if (dev && device_is_ready(dev)) {
		ina_dev = dev;
		ina_found = INA_3221;
		ina_num_channels = 3;
		ina_available = true;
		LOG_INF("Found power monitor: %s (INA3221, 3 channels)", dev->name);
		return 0;
	}

	/* INA219 — standalone single-channel */
	dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(ina219));
	if (dev && device_is_ready(dev)) {
		ina_dev = dev;
		ina_found = INA_219;
		ina_num_channels = 1;
		ina_available = true;
		LOG_INF("Found power monitor: %s (INA219)", dev->name);
		return 0;
	}

	/* ina2xx unified family — try all supported variants */
	dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(ina226));
	if (!dev || !device_is_ready(dev)) {
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(ina228));
	}
	if (!dev || !device_is_ready(dev)) {
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(ina230));
	}
	if (!dev || !device_is_ready(dev)) {
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(ina232));
	}
	if (!dev || !device_is_ready(dev)) {
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(ina236));
	}
	if (!dev || !device_is_ready(dev)) {
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(ina237));
	}
	if (dev && device_is_ready(dev)) {
		ina_dev = dev;
		ina_found = INA_2XX;
		ina_num_channels = 1;
		ina_available = true;
		LOG_INF("Found power monitor: %s (ina2xx)", dev->name);
		return 0;
	}

	LOG_INF("No power monitors found");
#endif

	return 0;
}

bool power_sensors_available(void)
{
#if HAS_ENV_SENSORS
	return ina_available;
#else
	return false;
#endif
}

int power_sensors_read(struct power_data *data)
{
	memset(data, 0, sizeof(*data));

#if HAS_ENV_SENSORS
	if (!ina_available || !ina_dev) {
		return -ENODEV;
	}

	struct sensor_value val;

	if (ina_found == INA_3221) {
		/* INA3221: iterate channels 1-3, select each before reading */
		data->num_channels = ina_num_channels;
		for (int ch = 0; ch < ina_num_channels; ch++) {
			struct sensor_value sel;
			sel.val1 = ch + 1;  /* INA3221 channels are 1-indexed */
			sel.val2 = 0;

			int rc = sensor_attr_set(ina_dev, SENSOR_CHAN_ALL,
			                         (enum sensor_attribute)SENSOR_ATTR_INA3221_SELECTED_CHANNEL,
			                         &sel);
			if (rc != 0) {
				continue;
			}

			rc = sensor_sample_fetch(ina_dev);
			if (rc != 0) {
				continue;
			}

			if (sensor_channel_get(ina_dev, SENSOR_CHAN_VOLTAGE, &val) == 0) {
				data->channels[ch].voltage_v = sensor_value_to_float(&val);
			}
			if (sensor_channel_get(ina_dev, SENSOR_CHAN_CURRENT, &val) == 0) {
				data->channels[ch].current_a = sensor_value_to_float(&val);
			}
			if (sensor_channel_get(ina_dev, SENSOR_CHAN_POWER, &val) == 0) {
				data->channels[ch].power_w = sensor_value_to_float(&val);
			}
			data->channels[ch].valid = true;
		}
	} else {
		/* INA219 / ina2xx unified: single-channel, straightforward read */
		data->num_channels = 1;
		int rc = sensor_sample_fetch(ina_dev);
		if (rc != 0) {
			return -EIO;
		}

		if (sensor_channel_get(ina_dev, SENSOR_CHAN_VOLTAGE, &val) == 0) {
			data->channels[0].voltage_v = sensor_value_to_float(&val);
		}
		if (sensor_channel_get(ina_dev, SENSOR_CHAN_CURRENT, &val) == 0) {
			data->channels[0].current_a = sensor_value_to_float(&val);
		}
		if (sensor_channel_get(ina_dev, SENSOR_CHAN_POWER, &val) == 0) {
			data->channels[0].power_w = sensor_value_to_float(&val);
		}
		data->channels[0].valid = true;
	}

	return 0;
#else
	return -ENOTSUP;
#endif
}
