#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

mkdir -p firmware

export ZEPHYR_SDK_INSTALL_DIR="${ZEPHYR_SDK_INSTALL_DIR:-$ROOT_DIR/zephyr-sdk-1.0.1}"
export CCACHE_DIR="${CCACHE_DIR:-$ROOT_DIR/.ccache}"

WEST="${WEST:-$ROOT_DIR/.venv/bin/west}"
BUILD_DIR="$ROOT_DIR/build_rpi_picow_room_server"
OUT_FILE="$ROOT_DIR/firmware/picow-room-server.uf2"

"$WEST" build -b rpi_pico/rp2040/w zephcore -d "$BUILD_DIR" --pristine -- \
	-DEXTRA_CONF_FILE=boards/common/room_server.conf

cp "$BUILD_DIR/zephyr/zephyr.uf2" "$OUT_FILE"
echo "Wrote $OUT_FILE"
