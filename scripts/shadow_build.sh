#!/usr/bin/env bash
set -euo pipefail

usage() {
	echo "usage: $0 <profile> <companion|repeater|bridge> [433|868] [--run]" >&2
	exit 2
}

(( $# >= 2 && $# <= 4 )) || usage

profile=$1
role=$2
variant=""
run=false
shift 2
for arg in "$@"; do
	case $arg in
		433|868) [[ -z $variant ]] || usage; variant=$arg ;;
		--run) run=true ;;
		*) usage ;;
	esac
done

platform=nrf
format=uf2
chip=""
sysbuild=false

case $profile in
	thinknode_m1|thinknode_m3|thinknode_m6|t1000_e|heltec_t114|rak4631|xiao_nrf52840)
		target=$profile
		stem=$profile
		;;
	lilygo_techo)
		target=lilygo_techo
		stem=lilygo_techo
		;;
	heltec_v3)
		target=heltec_wifi_lora32_v3/esp32s3/procpu
		stem=heltec_wifi_lora32_v3-esp32s3-procpu
		platform=esp32
		format=esp-sysbuild
		chip=esp32s3
		sysbuild=true
		;;
	*)
		echo "unknown profile: $profile" >&2
		usage
		;;
esac

confs=""
case $role in
	companion)
		role_tag=companion
		if [[ -n $variant ]]; then
			if [[ $profile == lilygo_techo && $variant == 433 ]]; then
				confs=boards/nrf52840/lilygo_techo/lilygo_techo_433.conf
				role_tag=companion-433
			else
				echo "unsupported companion variant: $profile $variant" >&2
				exit 2
			fi
		fi
		;;
	repeater)
		[[ -z $variant ]] || { echo "repeater variant is not defined" >&2; exit 2; }
		role_tag=repeater
		confs=boards/common/repeater.conf
		;;
	bridge)
		[[ $profile == thinknode_m6 || $profile == heltec_v3 ]] || {
			echo "bridge profile is defined only for thinknode_m6 and heltec_v3" >&2
			exit 2
		}
		[[ -n $variant ]] || { echo "bridge requires 433 or 868" >&2; exit 2; }
		role_tag=repeater-bridge-$variant
		if [[ $platform == nrf ]]; then
			[[ $variant == 868 ]] || { echo "thinknode_m6 bridge profile is 868" >&2; exit 2; }
			confs='boards/common/repeater.conf;boards/common/repeater_ble_bridge.conf;boards/common/repeater_ble_bridge_nrf_tx.conf'
		else
			confs='boards/common/repeater.conf;boards/common/repeater_espnow_bridge.conf;boards/common/repeater_ble_bridge_esp32_tx.conf'
			confs+=";boards/esp32/heltec_wifi_lora32_v3/heltec_v3_bridge_${variant}.conf"
		fi
		;;
	*) usage ;;
esac

build_dir="builds/${profile}-${role_tag}"
sdk_dir=${ZEPHYR_SDK_INSTALL_DIR:-"$PWD/zephyr-sdk-1.0.1"}
ccache_dir=${CCACHE_DIR:-"${TMPDIR:-/tmp}/zephcore-ccache"}
cmd=(./.venv/bin/west build -d "$build_dir" -b "$target" zephcore --pristine)
$sysbuild && cmd+=(--sysbuild)
[[ -n $confs ]] && cmd+=(-- -DEXTRA_CONF_FILE="$confs")

echo "board:    $target"
echo "role:     $role_tag"
echo "build:    $build_dir"
echo "artifact: firmware/${stem}-${role_tag}-<commit>"
printf 'command:  ZEPHYR_SDK_INSTALL_DIR=%q CCACHE_DIR=%q' \
	"$sdk_dir" "$ccache_dir"
printf ' %q' "${cmd[@]}"
printf '\n'

if ! $run; then
	echo "plan only; rerun with --run after confirmation"
	exit 0
fi

export ZEPHYR_SDK_INSTALL_DIR="$sdk_dir"
export CCACHE_DIR="$ccache_dir"
"${cmd[@]}"
scripts/package_firmware.sh "$build_dir" "$stem" "$role_tag" "$format" "$chip"
