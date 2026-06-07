/*
 * SPDX-License-Identifier: MIT
 * Zephyr CSPRNG implementation
 */

#pragma once

#include <mesh/RNG.h>
#include <mesh/Identity.h>
#include <stddef.h>

namespace mesh {

class ZephyrRNG : public RNG {
public:
	void random(uint8_t *dest, size_t sz) override;

	/* Layered entropy mixer for one-time first-boot identity-key
	 * generation. Combines:
	 *   1. sys_csrand_get (early)        — CSPRNG, strong on nRF/MG24
	 *   2. HWINFO unique device ID       — per-device uniqueness
	 *   3. Optional caller-supplied data — e.g. external noise samples
	 *   4. CPU cycle-counter jitter      — main entropy source
	 *                                       (NIST SP 800-90B class)
	 *   5. sys_csrand_get (late)         — catches mid-boot radio init
	 *   6. CPU cycle-counter jitter #2   — independent timing window
	 * Conditioned via AES-256-CTR (SHA-256(pool) → key, ECB on counter).
	 * NIST-style health checks on jitter samples; reboots on degenerate
	 * output. Blocks for ~250ms — only called at first-boot identity gen.
	 *
	 * Output is suitable as an Ed25519 seed regardless of platform
	 * TRNG state at boot. */
	static void mixIdentitySeed(uint8_t *out, size_t out_len,
				    const uint8_t *extra = nullptr,
				    size_t extra_len = 0);

	/* End-to-end first-boot identity generation. Mixes a fresh seed,
	 * derives the Ed25519 keypair, and retries (up to 100×) if the
	 * MeshCore protocol-reserved 0x00/0xFF public-key prefix happens
	 * to land. Panics-and-reboots on cap exhaustion (essentially
	 * impossible with a working CSPRNG: P(100 reserved in a row) ≈ 10⁻²¹¹).
	 * Wipes the intermediate seed before return.
	 *
	 * Use this from main()'s `loadIdentity` fall-through path instead
	 * of open-coding the mix+derive+retry+zeroize sequence. */
	static void generateFirstBootIdentity(LocalIdentity &out_identity);
};

} /* namespace mesh */
