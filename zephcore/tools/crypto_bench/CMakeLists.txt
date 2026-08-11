# SPDX-License-Identifier: MIT
# ZephCore crypto benchmark — is our crypto worth accelerating?
#
# Times every crypto primitive the mesh uses (SHA-256, AES-128-ECB,
# HMAC-SHA256, Ed25519, X25519), then divides the per-packet total by LoRa
# airtime. See HANDOVER_crypto_hw_accel.md — this tool is the "measure first"
# step that gates every option in that document.
#
# It compiles src/Utils.cpp, src/Identity.cpp and src/Packet.cpp straight from
# the main tree, so what is measured is the shipping implementation, not a
# copy of it.
#
# Build:
#   west build -b rak4631 zephcore/tools/crypto_bench --pristine
#   west build -b xiao_esp32s3/esp32s3/procpu zephcore/tools/crypto_bench --pristine
#
# Iteration counts:
#   west build ... zephcore/tools/crypto_bench -- -DCRYPTO_BENCH_ITERS=1000
#   west build ... zephcore/tools/crypto_bench -- -DCRYPTO_BENCH_ITERS_SLOW=100
#
# Group-channel worst case (default 40 = MAX_CHANNELS):
#   west build ... zephcore/tools/crypto_bench -- -DCRYPTO_BENCH_CHANNELS=8
#
# Flash: nRF52 — drag build/zephyr/zephyr.uf2 onto the UF2 drive
#        ESP32 — west flash --esp-device COMx
#
# The tool never reads or writes stored identity, and never brings up the
# radio, flash or BLE. Reflash normal firmware afterwards.

cmake_minimum_required(VERSION 3.20.0)

# Reuse the main project's custom board definitions
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# Strip the board qualifier (e.g. "/esp32s3/procpu") to get the base name
string(REPLACE "/" ";" BOARD_PARTS ${BOARD})
list(GET BOARD_PARTS 0 BOARD_BASE)

# Tool-local board overlay ONLY — deliberately not the node's board.overlay,
# which declares /fstab, radio, sensor and partition nodes this tool does not
# enable (CONFIG_FLASH=n, SPI=n, I2C=n) and would fail on an undefined
# lfs_partition label. Same arrangement as tools/rng_selftest.
set(_BENCH_OVERLAY
    "${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_BASE}/board.overlay")
if(EXISTS "${_BENCH_OVERLAY}")
    set(EXTRA_DTC_OVERLAY_FILE "${_BENCH_OVERLAY}" CACHE STRING "" FORCE)
    message(STATUS "crypto_bench overlay: ${_BENCH_OVERLAY}")
else()
    message(STATUS "crypto_bench: using upstream board console (no overlay)")
endif()

# Per-board conf (UF2 output, code partition) — nRF52 needs it, ESP32 must not
# have it. See boards/rak4631/board.conf.
set(_BENCH_CONF "${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_BASE}/board.conf")
if(EXISTS "${_BENCH_CONF}")
    set(EXTRA_CONF_FILE "${_BENCH_CONF}" CACHE STRING "" FORCE)
    message(STATUS "crypto_bench conf: ${_BENCH_CONF}")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephcore_crypto_bench)

# The point of this tool is to time the SHIPPING code. These three files are
# compiled from the main tree, at the same optimisation level the node uses,
# so the numbers transfer.
#
# ZephyrRNG.cpp is deliberately NOT linked: nothing here needs entropy (all
# test vectors are fixed, on purpose — see src/main.cpp), and pulling it in
# would drag CONFIG_ENTROPY_GENERATOR and hwinfo into a config that otherwise
# has no reason for them. The one thing it does that this tool needs —
# psa_crypto_init() — main.cpp calls itself.
target_sources(app PRIVATE
    src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/Utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/Identity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/Packet.cpp
)

# Ed25519 / X25519 backend — the real one Identity.cpp uses.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../lib/monocypher monocypher)
target_link_libraries(app PRIVATE monocypher)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/monocypher
)

# Iteration counts. The headline figure is the MINIMUM over N, so N mainly
# buys confidence that no run escaped interference; 200 is plenty for the
# symmetric primitives.
if(NOT DEFINED CRYPTO_BENCH_ITERS)
    set(CRYPTO_BENCH_ITERS 200)
endif()

# Ed25519 is ~1000x slower than SHA-256. 20 iterations keeps the total runtime
# in seconds; the variance on an operation that long is negligible anyway.
if(NOT DEFINED CRYPTO_BENCH_ITERS_SLOW)
    set(CRYPTO_BENCH_ITERS_SLOW 20)
endif()

# Worst-case group-channel loop (Mesh.cpp:398 tries every configured channel).
# Default matches MAX_CHANNELS.
if(NOT DEFINED CRYPTO_BENCH_CHANNELS)
    set(CRYPTO_BENCH_CHANNELS 40)
endif()

target_compile_definitions(app PRIVATE
    CRYPTO_BENCH_ITERS=${CRYPTO_BENCH_ITERS}
    CRYPTO_BENCH_ITERS_SLOW=${CRYPTO_BENCH_ITERS_SLOW}
    CRYPTO_BENCH_CHANNELS=${CRYPTO_BENCH_CHANNELS}
)
message(STATUS "crypto_bench: N=${CRYPTO_BENCH_ITERS} slow=${CRYPTO_BENCH_ITERS_SLOW} channels=${CRYPTO_BENCH_CHANNELS}")
