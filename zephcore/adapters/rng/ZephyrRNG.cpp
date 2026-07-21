/*
 * SPDX-License-Identifier: MIT
 */

#include "ZephyrRNG.h"
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/hwinfo.h>
#include <psa/crypto.h>
#include <stdint.h>
#include <string.h>
#include <mesh/Utils.h>

#if defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
/* Pre-RF entropy for the ESP32 HWRNG — see esp32_entropy_begin() below.
 * Source file is added to the build by CMakeLists.txt (ESP32 only). */
#include <bootloader_random.h>
#endif
namespace mesh {

#if !IS_ENABLED(CONFIG_CSPRNG_ENABLED)
static K_MUTEX_DEFINE(fallback_rng_mutex);
static bool fallback_rng_ready;
static uint64_t fallback_rng_state[4];

static uint64_t rotl64(uint64_t x, int k)
{
	return (x << k) | (x >> (64 - k));
}

static uint64_t fallback_rng_next_u64(void)
{
	const uint64_t result = rotl64(fallback_rng_state[1] * 5, 7) * 9;
	const uint64_t t = fallback_rng_state[1] << 17;

	fallback_rng_state[2] ^= fallback_rng_state[0];
	fallback_rng_state[3] ^= fallback_rng_state[1];
	fallback_rng_state[1] ^= fallback_rng_state[2];
	fallback_rng_state[0] ^= fallback_rng_state[3];
	fallback_rng_state[2] ^= t;
	fallback_rng_state[3] = rotl64(fallback_rng_state[3], 45);

	return result;
}

static void fallback_rng_init_locked(void)
{
	if (fallback_rng_ready) {
		return;
	}

	uint8_t seed[sizeof(fallback_rng_state)];
	ZephyrRNG::mixIdentitySeed(seed, sizeof(seed));
	memcpy(fallback_rng_state, seed, sizeof(fallback_rng_state));
	Utils::secureZeroize(seed, sizeof(seed));

	/* xoshiro256** must not be seeded with an all-zero state. The seed mixer
	 * already rejects degenerate output, but keep a local guard because this
	 * state persists for all later runtime nonces on no-CSPRNG platforms. */
	if ((fallback_rng_state[0] | fallback_rng_state[1] |
	     fallback_rng_state[2] | fallback_rng_state[3]) == 0) {
		Utils::cryptoPanicReboot("fallback RNG degenerate seed");
	}

	fallback_rng_ready = true;
}
#endif /* !CONFIG_CSPRNG_ENABLED */

void ZephyrRNG::random(uint8_t *dest, size_t sz)
{
#if IS_ENABLED(CONFIG_CSPRNG_ENABLED)
	/* Retry handles transient TRNG-warmup races; cold-reboot on persistent
	 * failure. Fabricating entropy here would silently produce weak keys
	 * forever (cf. Debian-OpenSSL 2008). k_msleep is illegal from ISR —
	 * all current callers run on main thread or syswq. */
	for (int attempt = 0; attempt < 4; attempt++) {
		if (sys_csrand_get(dest, sz) == 0) return;
		k_msleep(10);
	}
	Utils::cryptoPanicReboot("CSPRNG unavailable after retries");
#else
	/* RP2040 has no Zephyr entropy driver. Seed once with the same layered
	 * jitter/HWID/PSA conditioner used for first-boot identities, then expand
	 * cheaply for runtime packet tags/nonces. */
	k_mutex_lock(&fallback_rng_mutex, K_FOREVER);
	fallback_rng_init_locked();

	size_t pos = 0;
	while (pos < sz) {
		uint64_t block = fallback_rng_next_u64();
		size_t chunk = (sz - pos < sizeof(block)) ? (sz - pos) : sizeof(block);
		memcpy(dest + pos, &block, chunk);
		Utils::secureZeroize(&block, sizeof(block));
		pos += chunk;
	}
	k_mutex_unlock(&fallback_rng_mutex);
#endif
}

/* ===== Jitter sampling + online health check =============================
 *
 * Stephan Müller "CPU Time Jitter Based Non-Physical True Random Number
 * Generator" — Linux kernel's jitterentropy_rng. NIST SP 800-90B has
 * a compliance class for this entropy source type.
 *
 * Per-sample min-entropy on simple in-order embedded CPUs (Cortex-M,
 * RISC-V, Xtensa) is conservatively 0.1-0.3 bits per cycle-counter
 * delta. At 200ms × 160MHz / 1000 cycles per sample = 32,000 samples
 * × 0.1 bits = 3,200 estimated bits. 256 needed for Ed25519 → 12×
 * margin even pessimistically.
 *
 * Health check (NIST SP 800-90B style): online repetition count + a
 * distinct-value check tracked across all samples in the window with
 * scalar state — no per-sample buffer needed. Detects stuck-source
 * catastrophic failure (e.g. cycle counter not advancing). Does not
 * statistically prove entropy quality — that's what the literature is
 * for. */

/* Health statistics for one jitter window.  Timing statistics only — never
 * pool contents or derived key material.  Reporting these is standard practice
 * for a NIST SP 800-90B style noise source; reporting the bytes would not be. */
#define JITTER_HIST_SLOTS 16

struct jitter_stats {
	int      n_samples;
	int      n_distinct;   /* distinct delta values seen, capped at 8 */
	int      max_consec;   /* longest run of identical deltas */
	uint32_t min_delta;
	uint32_t max_delta;
	uint32_t mcv_count;    /* occurrences of the most common tracked delta */
	uint32_t untracked;    /* samples whose value missed the histogram      */
	bool     ok;
};

/* log2(x) * 1000, integer math (no FP — printk here has no float support).
 * Integer part from the leading bit; fraction by linear interpolation between
 * adjacent powers of two.  Linear interpolation UNDERSTATES log2 across that
 * range (log2(1.5)=0.585 vs 0.5 linear), so the derived entropy figure errs
 * low — the safe direction for an entropy claim. */
static uint32_t log2_millibits(uint32_t x)
{
	if (x <= 1) return 0;
	uint32_t ipart = 31u - (uint32_t)__builtin_clz(x);
	uint32_t base  = 1u << ipart;
	uint32_t frac  = (uint32_t)(((uint64_t)(x - base) * 1000u) / base);
	return ipart * 1000u + frac;
}

static bool sample_cpu_jitter(uint8_t *pool, size_t pool_size,
			      size_t pool_offset, uint32_t duration_ms,
			      struct jitter_stats *st = nullptr)
{
	uint32_t accum = k_cycle_get_32();
	int64_t deadline = k_uptime_get() + duration_ms;
	size_t idx = pool_offset;
	uint32_t min_delta = UINT32_MAX, max_delta = 0;

	/* Online health stats: 32 bytes total vs. the previous 512-byte
	 * deltas[] array. Tracks every sample, not just the first 128. */
	uint32_t prev_delta = 0;
	int cur_consec = 0, max_consec = 0;
	uint32_t distinct[8] = {0};
	int n_distinct = 0;
	int n_samples = 0;

	/* Bounded histogram for the SP 800-90B Most Common Value estimator.
	 * Holds the first JITTER_HIST_SLOTS distinct deltas; anything beyond
	 * that is counted in `untracked`.  A value frequent enough to dominate
	 * p_max shows up within the first few distinct observations with
	 * overwhelming probability, so this captures what the estimator needs
	 * — but `untracked` is reported so the assumption stays visible. */
	uint32_t hist_val[JITTER_HIST_SLOTS] = {0};
	uint32_t hist_cnt[JITTER_HIST_SLOTS] = {0};
	int n_hist = 0;
	uint32_t untracked = 0;

	while (k_uptime_get() < deadline) {
		uint32_t t1 = k_cycle_get_32();
		/* Variable-time work.  The iteration count comes from the
		 * accumulator, which carries the PREVIOUS measurement (see the
		 * feedback step below), so the amount of work done here depends
		 * on observed hardware nondeterminism rather than on a fixed
		 * sequence. */
		volatile uint32_t a = accum;
		uint32_t iters = (accum & 0x7f);
		for (uint32_t i = 0; i < iters; i++) {
			a = a * 1664525u + 1013904223u;
		}
		accum = a;
		uint32_t t2 = k_cycle_get_32();
		uint32_t delta = t2 - t1;

		/* FEEDBACK — the load-bearing line.  Without it `accum` evolves
		 * as a pure LCG from a single seed: the iteration count becomes a
		 * fixed sequence, and on any CPU where reading the cycle counter
		 * is cheap and same-domain (ESP32 CCOUNT) a warm cache makes each
		 * `iters` value yield an identical `delta`.  Deltas then repeat
		 * and the health check below correctly fails — which is exactly
		 * what was observed on every ESP32 board.
		 *
		 * Folding the measurement back in closes the loop, so timing
		 * variation propagates into subsequent work.  This is the
		 * mechanism the cited jitterentropy design relies on and which
		 * the original implementation omitted.
		 *
		 * nRF was unaffected: its k_cycle_get_32() is a 32.768 kHz RTC in
		 * a different clock domain, so the read latency itself varies
		 * with domain phase and supplied the nondeterminism this line
		 * now provides everywhere. */
		accum ^= delta;

		/* Mix into entropy pool */
		pool[idx++ % pool_size] ^= (uint8_t)delta;
		pool[idx++ % pool_size] ^= (uint8_t)(delta >> 8);
		pool[idx++ % pool_size] ^= (uint8_t)accum;
		pool[idx++ % pool_size] ^= (uint8_t)(accum >> 8);

		/* Online repetition count */
		if (n_samples > 0 && delta == prev_delta) {
			if (++cur_consec > max_consec) max_consec = cur_consec;
		} else {
			cur_consec = 1;
		}
		prev_delta = delta;

		/* Track first 8 distinct delta values */
		if (n_distinct < 8) {
			bool found = false;
			for (int j = 0; j < n_distinct; j++) {
				if (distinct[j] == delta) { found = true; break; }
			}
			if (!found) distinct[n_distinct++] = delta;
		}

		if (delta < min_delta) min_delta = delta;
		if (delta > max_delta) max_delta = delta;

		/* MCV histogram */
		{
			bool binned = false;
			for (int j = 0; j < n_hist; j++) {
				if (hist_val[j] == delta) {
					hist_cnt[j]++;
					binned = true;
					break;
				}
			}
			if (!binned) {
				if (n_hist < JITTER_HIST_SLOTS) {
					hist_val[n_hist] = delta;
					hist_cnt[n_hist] = 1;
					n_hist++;
				} else {
					untracked++;
				}
			}
		}

		n_samples++;
	}

	uint32_t mcv_count = 0;
	for (int j = 0; j < n_hist; j++) {
		if (hist_cnt[j] > mcv_count) mcv_count = hist_cnt[j];
	}

	bool ok = (n_samples >= 16)      /* enough samples          */
		&& (max_consec < 32)     /* not a stuck source      */
		&& (n_distinct >= 5);    /* minimal variance        */

	if (st) {
		st->n_samples  = n_samples;
		st->n_distinct = n_distinct;
		st->max_consec = max_consec;
		st->min_delta  = (n_samples > 0) ? min_delta : 0;
		st->max_delta  = max_delta;
		st->mcv_count  = mcv_count;
		st->untracked  = untracked;
		st->ok         = ok;
	}
	return ok;
}

/* Count distinct byte values in a buffer — a repetition/adaptive-proportion
 * style health indicator for a CSPRNG draw.  A stuck source collapses this to
 * 1.  Deliberately coarse: one integer per draw, which detects catastrophic
 * failure without meaningfully describing the bytes themselves. */
static int distinct_bytes(const uint8_t *buf, size_t len)
{
	bool seen[256] = {false};
	int n = 0;
	for (size_t i = 0; i < len; i++) {
		if (!seen[buf[i]]) { seen[buf[i]] = true; n++; }
	}
	return n;
}

/* One line per jitter window.  `distinct` is capped at 8 by the sampler, so 8/8
 * means "at least 8" — the pass threshold is 5.  `maxrep` is the longest run of
 * identical deltas; >=32 fails.  min/max delta expose a resolution problem: if
 * they are 0 and 1, the cycle counter is too coarse to measure the work at all
 * and no amount of sampling will help. */
static void report_jitter(const char *label, const struct jitter_stats *st)
{
	printk("[RNG] %s: samples=%d distinct=%d/8 maxrep=%d "
	       "delta=[%u..%u] -> %s\n",
	       label, st->n_samples, st->n_distinct, st->max_consec,
	       st->min_delta, st->max_delta, st->ok ? "PASS" : "FAIL");

	/* Min-entropy estimate, NIST SP 800-90B 6.3.1 Most Common Value:
	 *   p_max = mcv_count / n_samples
	 *   H_min per sample = -log2(p_max) = log2(n_samples / mcv_count)
	 *
	 * Computed in milli-bits with integer math. This REPLACES the 0.1-0.3
	 * bits/sample the file header used to assume — assumption is only valid
	 * if deltas actually vary, which is the very thing that failed on ESP32.
	 *
	 * Deliberately NOT measured on the conditioned output: AES-256-CTR makes
	 * any input look uniform, so output statistics would read perfect even
	 * for a near-zero-entropy seed. Entropy is a property of the source. */
	if (st->n_samples <= 0 || st->mcv_count == 0) {
		printk("[RNG]   entropy est: n/a (no samples)\n");
		return;
	}

	uint32_t ratio_q10 = (uint32_t)(((uint64_t)st->n_samples * 1024u)
					/ st->mcv_count);
	uint32_t per_mb = log2_millibits(ratio_q10);
	per_mb = (per_mb > 10000u) ? (per_mb - 10000u) : 0u;   /* less log2(1024) */

	uint64_t total_mb = (uint64_t)per_mb * (uint64_t)st->n_samples;
	uint32_t total_bits = (uint32_t)(total_mb / 1000u);

	printk("[RNG]   entropy est: %u.%03u bits/sample x %d = ~%u bits "
	       "(need 256)%s\n",
	       per_mb / 1000u, per_mb % 1000u, st->n_samples, total_bits,
	       st->untracked ? " [histogram overflowed — est. is optimistic]" : "");
}

/* ===== Entropy extraction via AES-256-CTR ================================
 *
 * Per crypto consultant (MeshCore upstream PR#2280 author): the
 * conditioning step is most correctly an XOF or stream cipher, not a
 * truncated hash. For our 32-byte Ed25519-seed output the difference
 * is design hygiene rather than security, but the cost is the same
 * order of magnitude (~one SHA-512 vs SHA-256 + two AES-ECB blocks).
 *
 * Construction (NIST SP 800-108 KDF-in-Counter-Mode style):
 *   1. Extract: SHA-256(pool) → 32-byte AES-256 key.
 *   2. Expand:  AES-256-ECB(counter_i) for counter_i = 0, 1, 2 ...
 *               output = concatenation of ciphertext blocks.
 * Plaintext-XOR (true CTR mode) is omitted because plaintext would be
 * all-zero — we want just the keystream.
 *
 * Uses PSA crypto API (already enabled via PSA_WANT_KEY_TYPE_AES +
 * PSA_WANT_ALG_ECB_NO_PADDING in zephcore_common.conf).
 */
static int extract_via_aes_ctr(const uint8_t *pool, size_t pool_len,
			       uint8_t *out, size_t out_len)
{
	psa_status_t status;
	uint8_t key[32];
	size_t key_len = 0;

	/* PSA is idempotent — already initialized via mbedTLS but a defensive
	 * call here costs nothing if it returns PSA_ERROR_ALREADY_EXISTS. */
	(void)psa_crypto_init();

	/* Extract: SHA-256(pool) → AES key.  Open-coded here (NOT Utils::sha256)
	 * on purpose: that wrapper returns void and silently zeroes its output on
	 * PSA failure.  A zeroed key imports fine and AES-ECB(key=0) derives a
	 * fixed, device-independent seed that the all-zero/all-FF degenerate check
	 * cannot catch — every affected unit would share one Ed25519 identity.  We
	 * must hard-fail so the caller (mixIdentitySeed) cryptoPanicReboots. */
	status = psa_hash_compute(PSA_ALG_SHA_256, pool, pool_len,
				  key, sizeof(key), &key_len);
	if (status != PSA_SUCCESS || key_len != sizeof(key)) {
		Utils::secureZeroize(key, sizeof(key));
		return -1;
	}

	/* Import key for AES-256-ECB */
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
	psa_set_key_algorithm(&attr, PSA_ALG_ECB_NO_PADDING);
	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT);
	psa_set_key_bits(&attr, 256);

	psa_key_id_t key_id = 0;
	status = psa_import_key(&attr, key, sizeof(key), &key_id);
	/* Wipe stack-resident AES key — secureZeroize survives -Os DSE. */
	Utils::secureZeroize(key, sizeof(key));
	if (status != PSA_SUCCESS) {
		return -1;
	}

	/* Expand: AES-ECB(counter_i) for i = 0, 1, ... */
	uint8_t counter[16] = {0};
	size_t pos = 0;
	int ret = 0;
	while (pos < out_len) {
		uint8_t block[16];
		size_t block_out = 0;
		status = psa_cipher_encrypt(key_id, PSA_ALG_ECB_NO_PADDING,
					    counter, sizeof(counter),
					    block, sizeof(block), &block_out);
		if (status != PSA_SUCCESS || block_out != sizeof(block)) {
			ret = -1;
			break;
		}
		size_t chunk = (out_len - pos < sizeof(block))
			? (out_len - pos) : sizeof(block);
		memcpy(out + pos, block, chunk);
		pos += chunk;

		/* Increment 128-bit counter, big-endian — overflow rolls over.
		 * For our 32-byte output we only ever hit counters 0 and 1. */
		for (int i = sizeof(counter) - 1; i >= 0; i--) {
			if (++counter[i] != 0) break;
		}
		Utils::secureZeroize(block, sizeof(block));
	}

	psa_destroy_key(key_id);
	Utils::secureZeroize(counter, sizeof(counter));
	return ret;
}

void ZephyrRNG::mixIdentitySeed(uint8_t *out, size_t out_len,
				const uint8_t *extra, size_t extra_len)
{
	uint8_t pool[512];
	memset(pool, 0, sizeof(pool));

	/* ESP32 only: give the HWRNG a real entropy source for the duration of
	 * this function.
	 *
	 * WDEV_RANDOM is a PRNG that receives hardware entropy only "provided
	 * Wi-Fi or BT are enabled" (Zephyr drivers/entropy/entropy_esp32.c), and
	 * sys_csrand_get() maps straight to it. Every caller of this function
	 * runs before RF is up, and repeater / room-server builds never enable
	 * RF at all — so stages 1 and 5 below contributed NOTHING on ESP32,
	 * leaving CPU jitter as the only real source. That was observed failing
	 * its health check on ThinkNode M9 hardware while deriving a permanent
	 * identity key.
	 *
	 * bootloader_random_enable() puts the SAR ADC into continuous sampling
	 * and mixes its noise into the HWRNG; Espressif's header explicitly
	 * sanctions calling it from app code when RF is not up. It must be
	 * disabled again before anything else touches the ADC or RF — done at
	 * the end of the collection phase, before AES extraction, so the ADC is
	 * held for as short a window as possible.
	 *
	 * WARNING for future callers: this is unsafe if RF or the ADC is already
	 * in use. Do not call mixIdentitySeed() after bt_enable() or alongside a
	 * battery read on ESP32. */
#if defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
	bootloader_random_enable();
	printk("[RNG] === identity seed health ===\n");
	printk("[RNG] esp32 pre-RF entropy (bootloader_random): ENABLED\n");
#else
	printk("[RNG] === identity seed health ===\n");
	printk("[RNG] platform TRNG is radio-independent (no pre-RF workaround needed)\n");
#endif

	/* Stage 1: early CSPRNG (strong on nRF/MG24; on ESP32 this is only real
	 * because bootloader_random_enable() above is feeding the HWRNG) */
	int rc1 = sys_csrand_get(pool, 64);
	printk("[RNG] stage1 csrand    : rc=%d distinct=%d/64\n",
	       rc1, distinct_bytes(pool, 64));

	/* Stage 2: HWINFO unique device ID — uniqueness across devices */
	uint8_t devid[16] = {0};
	ssize_t devid_len = hwinfo_get_device_id(devid, sizeof(devid));
	for (ssize_t i = 0; i < devid_len && i < (ssize_t)sizeof(devid); i++) {
		pool[64 + i] ^= devid[i];
	}
	/* NOT secret — this is the efuse/FICR serial, public and printed at boot.
	 * It contributes uniqueness between devices, never unpredictability. */
	printk("[RNG] stage2 hwinfo id  : %d bytes (public — uniqueness only)\n",
	       (int)devid_len);

	/* Stage 3: caller-supplied entropy (e.g. ADC LSB noise) */
	if (extra && extra_len > 0) {
		size_t n = (extra_len < 32) ? extra_len : 32;
		for (size_t i = 0; i < n; i++) pool[80 + i] ^= extra[i];
		printk("[RNG] stage3 extra     : %d bytes\n", (int)n);
	} else {
		printk("[RNG] stage3 extra     : none\n");
	}

	/* Stage 4: CPU cycle-counter jitter, 200ms.
	 *
	 * Skipped on POSIX arch (native_sim / Linux): the simulated clock only
	 * advances when Zephyr threads yield, so k_uptime_get() is frozen while
	 * this loop spins → infinite loop.  On Linux we have /dev/urandom (via
	 * sys_csrand_get in stages 1 and 5) which is a far stronger source than
	 * jitter sampling anyway. */
#ifndef CONFIG_ARCH_POSIX
	struct jitter_stats js = {};
	bool health_ok = sample_cpu_jitter(pool, sizeof(pool), 112, 200, &js);
	report_jitter("stage4 jitter 200ms", &js);
	if (!health_ok) {
		printk("[RNG] stage4 FAILED — resampling at 400ms\n");
		health_ok = sample_cpu_jitter(pool, sizeof(pool), 112, 400, &js);
		report_jitter("stage4 jitter 400ms", &js);
		if (!health_ok) {
			printk("[RNG] stage4 STILL FAILING — continuing with mixed sources\n");
		}
	}
#endif /* CONFIG_ARCH_POSIX */

	/* Stage 5: late CSPRNG — catches any mid-boot radio init that
	 * warmed the TRNG during the 200ms jitter window */
	int rc5 = sys_csrand_get(pool + 368, 64);
	printk("[RNG] stage5 csrand    : rc=%d distinct=%d/64\n",
	       rc5, distinct_bytes(pool + 368, 64));

	/* Stage 6: second jitter sample, independent timing window */
#ifndef CONFIG_ARCH_POSIX
	struct jitter_stats js6 = {};
	(void)sample_cpu_jitter(pool, sizeof(pool), 432, 50, &js6);
	report_jitter("stage6 jitter  50ms", &js6);
#endif /* CONFIG_ARCH_POSIX */

	/* Collection done — release the SAR ADC before anything else needs it.
	 * Unconditional: every path below this point either returns normally or
	 * reboots, so there is no path that leaves it enabled. */
#if defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
	bootloader_random_disable();
	printk("[RNG] esp32 pre-RF entropy: DISABLED (ADC released)\n");
#endif
	printk("[RNG] === end (extracting %u bytes via AES-256-CTR) ===\n",
	       (unsigned)out_len);

	/* Final conditioning: AES-256-CTR over the pool. Extracts a 32-byte
	 * AES key via SHA-256(pool), then expands to out_len bytes via
	 * AES-ECB on a 128-bit counter. Per crypto consultant guidance —
	 * see extract_via_aes_ctr() for full rationale. */
	if (extract_via_aes_ctr(pool, sizeof(pool), out, out_len) != 0) {
		Utils::cryptoPanicReboot("AES-CTR seed extraction failed");
	}

	/* Output sanity check — reject all-zero / all-0xFF (catastrophic
	 * failure of every source). */
	bool all_zero = true, all_ff = true;
	for (size_t i = 0; i < out_len; i++) {
		if (out[i] != 0x00) all_zero = false;
		if (out[i] != 0xFF) all_ff = false;
	}
	if (all_zero || all_ff) {
		Utils::cryptoPanicReboot("degenerate seed output (all-zero / all-FF)");
	}

	/* Wipe sensitive intermediate buffers — secureZeroize survives the
	 * -Os dead-store-elimination that would silently elide plain memset
	 * on stack locals that are never read again. */
	Utils::secureZeroize(pool, sizeof(pool));
	Utils::secureZeroize(devid, sizeof(devid));
}

void ZephyrRNG::generateFirstBootIdentity(LocalIdentity &out_identity)
{
	uint8_t seed[32];

	mixIdentitySeed(seed, sizeof(seed));
	out_identity.fromSeed(seed);

	/* Reserved-prefix guard — MeshCore protocol treats pub_key[0] of
	 * 0x00/0xFF as reserved markers. With a working CSPRNG the first
	 * attempt almost always passes (P(reserved) = 2/256); the cap +
	 * panic-reboot is a stuck-source backstop. */
	int attempt = 0;
	while (out_identity.pub_key[0] == 0x00 || out_identity.pub_key[0] == 0xFF) {
		if (++attempt > 100) {
			Utils::cryptoPanicReboot("identity gen stuck on reserved prefix");
		}
		mixIdentitySeed(seed, sizeof(seed));
		out_identity.fromSeed(seed);
	}

	Utils::secureZeroize(seed, sizeof(seed));
}

} /* namespace mesh */
