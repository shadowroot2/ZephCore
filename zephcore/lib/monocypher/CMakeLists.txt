# SPDX-License-Identifier: MIT
# Vendored Monocypher 4.0.2 (https://monocypher.org) — license CC0-1.0 OR
# BSD-2-Clause. Provides RFC 8032 Ed25519 (SHA-512) + X25519. Replaces the
# previous orlp/ed25519 vendored library; the Identity layer drives sign/keygen
# from the stored 64-byte expanded key via the low-level EdDSA primitives, so
# existing on-disk and on-wire keys remain byte-compatible (see src/Identity.cpp).
#
# Upstream: src/monocypher.{c,h} + src/optional/monocypher-ed25519.{c,h}, copied
# verbatim with only the "version __git__" placeholder pinned to "4.0.2".

# Application mode: zephyr_library_named() only warns here (ZEPHYR_LIBS is
# unused in app mode); zephyr_interface keeps the same compile flags/includes.
add_library(monocypher STATIC
    monocypher.c
    monocypher-ed25519.c
)
target_include_directories(monocypher PUBLIC .)
target_link_libraries(monocypher PUBLIC zephyr_interface)
