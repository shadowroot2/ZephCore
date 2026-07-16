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

/* --- Adaptive CAD (LBT detPeak calibration) ---
 * Housekeeping-tick CAD probes accumulate per-level busy/free statistics;
 * a one-sided staircase converges on the lowest detPeak offset whose
 * false-positive rate stays under target.  Levels are signed offsets from
 * the chip family's per-SF base detPeak (SX126x: SF+13; LR11xx/LR20xx:
 * 56-68 table) so the C++ layer stays scale-independent. */
/* Operating-offset range (levels from the per-SF family base detPeak).  Wide
 * on purpose — a dense hilltop may need a much higher detPeak than a quiet
 * valley node; the per-family absolute clamp in the driver (SX126x 15-40,
 * LR 48-90) is a firmware guardrail against "CAD never/always fires", NOT a
 * chip limit (cadDetPeak is a full uint8_t).
 * MUST match CAD_OFFSET_MIN/MAX in helpers/NodePrefs.h. */
#define CAD_LEVEL_MIN            (-8)  /* most sensitive probe level */
#define CAD_LEVEL_MAX            12    /* least sensitive probe level */
#define CAD_NUM_LEVELS           (CAD_LEVEL_MAX - CAD_LEVEL_MIN + 1)
#define CAD_SWEEP_MIN            (-4)  /* dry-run sweep window (get cad with auto off) */
#define CAD_SWEEP_MAX            4
/* Knee-seeking staircase (replaces the earlier absolute-FP-target band).  The
 * FP-vs-detPeak curve falls as detPeak rises (less sensitive → fewer false
 * detects) and flattens past a knee; the sweet spot is the knee — the most
 * sensitive detPeak whose FP has already bottomed out.  The controller reads
 * the local curve SLOPE from three rungs (frontier op-1, operating op, op+1)
 * rather than an absolute FP level, so it converges the same way regardless of
 * a site's FP floor (which varies with traffic and classifier residual).
 *  - KNEE_SLOPE: the per-level FP change (permille) that counts as "steep".
 *    Below the knee the curve drops fast (step up toward the knee); at/above it
 *    the curve is flat (slope < KNEE_SLOPE).
 *  - PLATEAU_CLEAN: on a flat plateau, only reclaim sensitivity (step down) if
 *    FP is already this low — the guard that stops a flat-but-noisy curve from
 *    walking to the sensitive rail (there, holding is the least-bad move; a
 *    genuinely quiet flat-low site descends to the floor, which is correct). */
#define CAD_KNEE_SLOPE_PERMILLE     50    /* >=5%/level FP change = steep */
#define CAD_PLATEAU_CLEAN_PERMILLE  50    /* <=5% FP = clean enough to descend */
#define CAD_STEP_MIN_PROBES         120   /* per-level samples before a step call */
/* Airtime-protection cap on the TOTAL busy (defer) rate — false positives AND
 * real traffic.  The knee controller only minimises *false* busy, but on a
 * congested hilltop most busy verdicts are real distant traffic we'd never
 * actually collide with (capture effect), and deferring for all of it starves
 * the node's own airtime.  When the operating level's busy rate exceeds the
 * cap, step UP (less sensitive) regardless of FP — self-targeting, since a
 * quiet node's busy rate never reaches it.  HYST keeps a descend from bouncing
 * straight back into the cap.  The cap itself is a per-node pref
 * (`cad_busycap`, percent, `set cad.busycap`; default 25, 0 = off) since it is
 * a policy call (airtime vs. collision/capture), not a physical constant. */
#define CAD_BUSY_DEFER_HYST_PERMILLE 100  /* descend only if frontier busy <= cap-10% */
#define CAD_PROBE_RSSI_GUARD     7     /* dB above floor = channel visibly busy, skip probe */
#define CAD_STATS_DECAY_MS       (6UL * 3600UL * 1000UL)  /* halve counters every 6 h */

/* --- RX ring buffer --- */
#define RX_RING_SIZE 8  /* ~2 KB; buffers burst arrivals at SF7/BW500 */

/* --- TX wait thread --- */
#define TX_WAIT_THREAD_STACK_SIZE 2048
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
