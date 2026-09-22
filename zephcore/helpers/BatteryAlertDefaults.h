/* SPDX-License-Identifier: MIT */
#pragma once

/* Repeater alert defaults. nRF52 uses the requested 3250 mV threshold.
 * Other platforms retain 3500 mV. This does not enable auto-shutdown. */
#if defined(CONFIG_SOC_SERIES_NRF52)
#define ZEPHCORE_BATTERY_ALERT_DEFAULT_MV 3250
#else
#define ZEPHCORE_BATTERY_ALERT_DEFAULT_MV 3500
#endif
