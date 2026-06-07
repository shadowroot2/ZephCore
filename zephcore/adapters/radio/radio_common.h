/*
 * SPDX-License-Identifier: MIT
 * Shared constants and utilities for all LoRa radio adapters.
 *
 * Anything duplicated between SX126xRadio and LR1110Radio belongs here.
 * Radio-specific constants (e.g. SX126x duty cycle math) stay in
 * their respective headers.
 */

#pragma once

#include <zephyr/drivers/lora.h>

/* --- Noise floor calibration (EMA) ---
 * Median-of-N RSSI reads → EMA.  alpha = 1/8, converges in ~8 ticks (~40s).
 * Samples above floor + SAMPLING_THRESHOLD rejected as interference. */
#define NOISE_FLOOR_EMA_SHIFT            3   /* alpha = 1/(1<<3) = 1/8 */
#define NOISE_FLOOR_SAMPLES_PER_TICK     8   /* median of 8 reads per tick */
#define NOISE_FLOOR_UNGUARDED_INTERVAL   16  /* ticks between unfiltered samples (power of 2) */
#define NOISE_FLOOR_SAMPLING_THRESHOLD   14  /* dB above floor to reject as interference */
#define DEFAULT_NOISE_FLOOR              0   /* sentinel: seed from first sample */

/* --- RX ring buffer --- */
#define RX_RING_SIZE 8  /* ~2 KB; buffers burst arrivals at SF7/BW500 */

/* --- TX wait thread --- */
#define TX_WAIT_THREAD_STACK_SIZE 1024
#define TX_WAIT_THREAD_PRIORITY   10     /* preemptible, below main thread */
#define TX_TIMEOUT_MS             5000   /* hard timeout for TX completion signal */

/* --- SNR thresholds per spreading factor (SF7..SF12) --- */
inline constexpr float lora_snr_threshold[] = {
	-7.5f, -10.0f, -12.5f, -15.0f, -17.5f, -20.0f
};

/* --- Callback types --- */
typedef void (*RadioRxCallback)(void *user_data);
typedef void (*RadioTxDoneCallback)(void *user_data);

/* --- Zephyr enum mapping --- */


static inline uint32_t bandwidth_to_hz(enum lora_signal_bandwidth bw)
{
	switch (bw) {
	case BW_7_KHZ:   return 7812;
	case BW_10_KHZ:  return 10417;
	case BW_15_KHZ:  return 15625;
	case BW_20_KHZ:  return 20833;
	case BW_31_KHZ:  return 31250;
	case BW_41_KHZ:  return 41667;
	case BW_62_KHZ:  return 62500;
	case BW_125_KHZ: return 125000;
	case BW_250_KHZ: return 250000;
	case BW_500_KHZ: return 500000;
	default:         return 125000;
	}
}

/* Input is truncated kHz (e.g. 7.8→7, 10.4→10, 62.5→62) */
static inline enum lora_signal_bandwidth bw_khz_to_enum(uint16_t bw_khz)
{
	switch (bw_khz) {
	case 7:   return BW_7_KHZ;
	case 10:  return BW_10_KHZ;
	case 15:  return BW_15_KHZ;
	case 20:  return BW_20_KHZ;
	case 31:  return BW_31_KHZ;
	case 41:  return BW_41_KHZ;
	case 62:  return BW_62_KHZ;
	case 125: return BW_125_KHZ;
	case 250: return BW_250_KHZ;
	case 500: return BW_500_KHZ;
	default:  return BW_125_KHZ;
	}
}

/* CR 5-8 → Zephyr coding_rate enum */
static inline enum lora_coding_rate cr_to_enum(uint8_t cr)
{
	switch (cr) {
	case 5: return CR_4_5;
	case 6: return CR_4_6;
	case 7: return CR_4_7;
	case 8: return CR_4_8;
	default: return CR_4_5;
	}
}
