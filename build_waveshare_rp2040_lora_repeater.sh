#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

mkdir -p firmware

export ZEPHYR_SDK_INSTALL_DIR="${ZEPHYR_SDK_INSTALL_DIR:-$ROOT_DIR/zephyr-sdk-1.0.1}"
export CCACHE_DIR="${CCACHE_DIR:-$ROOT_DIR/.ccache}"

WEST="${WEST:-$ROOT_DIR/.venv/bin/west}"
BUILD_DIR="$ROOT_DIR/build_rpi_pico_repeater"
OUT_FILE="$ROOT_DIR/firmware/picow-repeater.uf2"
LEGACY_OUT_FILE="$ROOT_DIR/firmware/waveshare_rp2040_lora-repeater.uf2"

"$WEST" build -b rpi_pico zephcore -d "$BUILD_DIR" --pristine -- \
	-DEXTRA_CONF_FILE=boards/common/repeater.conf

cp "$BUILD_DIR/zephyr/zephyr.uf2" "$OUT_FILE"
cp "$BUILD_DIR/zephyr/zephyr.uf2" "$LEGACY_OUT_FILE"
echo "Wrote $OUT_FILE"
echo "Wrote $LEGACY_OUT_FILE"
