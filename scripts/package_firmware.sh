#!/usr/bin/env bash
set -euo pipefail

if (( $# < 4 || $# > 5 )); then
	echo "usage: $0 <build-dir> <artifact-stem> <role-tag> <uf2|esp-sysbuild> [chip]" >&2
	exit 2
fi

build_dir=$1
stem=$2
role_tag=$3
format=$4
chip=${5:-}
commit=$(git rev-parse --short HEAD)
out="firmware/${stem}-${role_tag}-${commit}"

mkdir -p firmware

case $format in
	uf2)
		src="${build_dir}/zephyr/zephyr.uf2"
		[[ -f $src ]] || { echo "missing $src" >&2; exit 1; }
		cp "$src" "${out}.uf2"
		echo "${out}.uf2"
		;;
	esp-sysbuild)
		[[ -n $chip ]] || { echo "chip is required for esp-sysbuild" >&2; exit 2; }
		boot="${build_dir}/mcuboot/zephyr/zephyr.bin"
		app="${build_dir}/zephcore/zephyr/zephyr.signed.bin"
		dts="${build_dir}/zephcore/zephyr/zephyr.dts"
		app_config="${build_dir}/zephcore/zephyr/.config"
		boot_config="${build_dir}/mcuboot/zephyr/.config"
		[[ -f $boot && -f $app && -f $dts && -f $app_config && -f $boot_config ]] || {
			echo "incomplete sysbuild output in $build_dir" >&2
			exit 1
		}
		flash_size=$(python3 zephcore/scripts/dts_flash_size.py "$dts")
		app_offset=$(sed -n 's/^CONFIG_FLASH_LOAD_OFFSET=//p' "$app_config")
		flash_mode=$(sed -n 's/^CONFIG_ESPTOOLPY_FLASHMODE="\([^"]*\)"/\1/p' "$boot_config")
		flash_freq=$(sed -n 's/^CONFIG_ESPTOOLPY_FLASHFREQ="\([^"]*\)"/\1/p' "$boot_config")
		python3 -m esptool --chip "$chip" merge-bin \
			--output "${out}-merged.bin" \
			--flash-mode "$flash_mode" --flash-freq "$flash_freq" --flash-size "$flash_size" \
			0x00000 "$boot" "$app_offset" "$app"
		cp "$app" "${out}-update.bin"
		echo "${out}-merged.bin"
		echo "${out}-update.bin"
		;;
	*)
		echo "unknown format: $format" >&2
		exit 2
		;;
esac
