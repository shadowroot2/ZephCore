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
#include <string.h>
#include <mesh/Utils.h>

BUILD_ASSERT(IS_ENABLED(CONFIG_CSPRNG_ENABLED),
	"ZephyrRNG requires CONFIG_CSPRNG_ENABLED for cryptographic key derivation");

namespace mesh {

void ZephyrRNG::random(uint8_t *dest, size_t sz)
{
	/* Retry handles transient TRNG-warmup races; cold-reboot on persistent
	 * failure. Fabricating entropy here would silently produce weak keys
	 * forever (cf. Debian-OpenSSL 2008). k_msleep is illegal from ISR —
	 * all current callers run on main thread or syswq. */
	for (int attempt = 0; attempt < 4; attempt++) {
		if (sys_csrand_get(dest, sz) == 0) return;
		k_msleep(10);
	}
	Utils::cryptoPanicReboot("CSPRNG unavailable after retries");
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

static bool sample_cpu_jitter(uint8_t *pool, size_t pool_size,
			      size_t pool_offset, uint32_t duration_ms)
{
	uint32_t accum = k_cycle_get_32();
	int64_t deadline = k_uptime_get() + duration_ms;
	size_t idx = pool_offset;

	/* Online health stats: 32 bytes total vs. the previous 512-byte
	 * deltas[] array. Tracks every sample, not just the first 128. */
	uint32_t prev_delta = 0;
	int cur_consec = 0, max_consec = 0;
	uint32_t distinct[8] = {0};
	int n_distinct = 0;
	int n_samples = 0;

	while (k_uptime_get() < deadline) {
		uint32_t t1 = k_cycle_get_32();
		/* Variable-time work — number of iterations depends on the
		 * accumulator, so timing depends on hardware nondeterminism
		 * (cache, branch prediction, ISR firing). */
		volatile uint32_t a = accum;
		uint32_t iters = (accum & 0x7f);
		for (uint32_t i = 0; i < iters; i++) {
			a = a * 1664525u + 1013904223u;
		}
		accum = a;
		uint32_t t2 = k_cycle_get_32();
		uint32_t delta = t2 - t1;

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

		n_samples++;
	}

	if (n_samples < 16) return false;
	if (max_consec >= 32) return false;  /* stuck source */
	return n_distinct >= 5;               /* minimal variance */
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

	/* Stage 1: early CSPRNG (strong on nRF/MG24, weak on ESP32 pre-radio) */
	(void)sys_csrand_get(pool, 64);

	/* Stage 2: HWINFO unique device ID — uniqueness across devices */
	uint8_t devid[16] = {0};
	ssize_t devid_len = hwinfo_get_device_id(devid, sizeof(devid));
	for (ssize_t i = 0; i < devid_len && i < (ssize_t)sizeof(devid); i++) {
		pool[64 + i] ^= devid[i];
	}

	/* Stage 3: caller-supplied entropy (e.g. ADC LSB noise) */
	if (extra && extra_len > 0) {
		size_t n = (extra_len < 32) ? extra_len : 32;
		for (size_t i = 0; i < n; i++) pool[80 + i] ^= extra[i];
	}

	/* Stage 4: CPU cycle-counter jitter, 200ms.
	 *
	 * Skipped on POSIX arch (native_sim / Linux): the simulated clock only
	 * advances when Zephyr threads yield, so k_uptime_get() is frozen while
	 * this loop spins → infinite loop.  On Linux we have /dev/urandom (via
	 * sys_csrand_get in stages 1 and 5) which is a far stronger source than
	 * jitter sampling anyway. */
#ifndef CONFIG_ARCH_POSIX
	bool health_ok = sample_cpu_jitter(pool, sizeof(pool), 112, 200);
	if (!health_ok) {
		printk("ZephyrRNG: jitter health check failed, resampling 400ms\n");
		health_ok = sample_cpu_jitter(pool, sizeof(pool), 112, 400);
		if (!health_ok) {
			printk("ZephyrRNG: jitter health still failing — continuing with mixed sources\n");
		}
	}
#endif /* CONFIG_ARCH_POSIX */

	/* Stage 5: late CSPRNG — catches any mid-boot radio init that
	 * warmed the TRNG during the 200ms jitter window */
	(void)sys_csrand_get(pool + 368, 64);

	/* Stage 6: second jitter sample, independent timing window */
#ifndef CONFIG_ARCH_POSIX
	(void)sample_cpu_jitter(pool, sizeof(pool), 432, 50);
#endif /* CONFIG_ARCH_POSIX */

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
