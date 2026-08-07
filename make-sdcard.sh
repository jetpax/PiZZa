#!/usr/bin/env bash
#
# SPDX-License-Identifier: Apache-2.0
#
# Build a flashable single-FAT32 SD image for any PiZZa Zephyr app.
# The Pi boots a Zephyr kernel straight from a
# FAT boot partition, so the image is just: the Raspberry Pi boot
# blobs + config.txt + the app (zephyr.bin) + optional payload files
# (game assets, disk images). Flash the result with Raspberry Pi
# Imager (Use custom) or balenaEtcher.
#
# This is the canonical way to create a card; install-to-sdcard.sh
# remains the quick way to swap zephyr.bin on an existing card.
#
# Loop-free (uses mtools), so it runs in a plain Docker container --
# no root, no privileged mode, works the same on macOS and Linux.
#
# Usage:
#   Single app:
#     ./make-sdcard.sh <rpi_zero_2w|rpi_zero_w> <zephyr.bin> [-o NAME.img]
#                      [-s SIZE_MB] [payload files...]
#   Multi-app boot-menu card (WO-T1):
#     ./make-sdcard.sh <board> --menu <bootmenu.bin> "Name=path/zephyr.bin"
#                      [more "Name=path" entries] [-o ...] [-s ...] [payload...]
#
# Multi-app images stage PiZZaBoot as the config.txt default kernel, one
# renamed kernel per entry (name slugged to <slug>.bin), a generated
# menu.txt (first entry = the default, 5 s countdown), and the
# [gpio17=0] force-menu conditional (button GPIO17 -> GND, held at
# power-on). No chosen.txt is staged: first boot shows the menu.
#
# Examples:
#   ./make-sdcard.sh rpi_zero_2w ~/zephyrproject/build/pizzashell/zephyr/zephyr.bin
#   ./make-sdcard.sh rpi_zero_2w build/doom/zephyr/zephyr.bin -o pizza-doom.img doom.img
#   ./make-sdcard.sh rpi_zero_2w --menu build-pizzaboot/zephyr/zephyr.bin \
#       "PiZZa Shell=build-shell/zephyr/zephyr.bin" \
#       "RetroPiZZa=build-retro-hw/zephyr/zephyr.bin" -o pizza-menu.img

set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

# raspberrypi/firmware commit the boot blobs are pinned to (reproducible
# builds).
FW_PIN="afc8dbd74865c6d367dba5505c5a863252588ca8"

BOARD="${1:?usage: $0 <rpi_zero_2w|rpi_zero_w> <zephyr.bin>|--menu <bootmenu.bin> <Name=path...> [-o out.img] [-s size_mb] [payload...]}"
shift
MENU_MODE=0
ZEPHYR_BIN=""
BOOTMENU_BIN=""
if [ "${1:-}" = "--menu" ]; then
	MENU_MODE=1
	BOOTMENU_BIN="${2:?path to the PiZZaBoot zephyr.bin required after --menu}"
	shift 2
else
	ZEPHYR_BIN="${1:?path to zephyr.bin required}"
	shift
fi

# The firmware picks its device tree from the board revision word, so a
# card must carry every DTB the boards it targets can ask for. The
# original Pi Zero (no Wi-Fi) is the same BCM2835 as the Zero W but asks
# for bcm2708-rpi-zero.dtb, so rpi_zero_w cards ship both.
case "${BOARD}" in
rpi_zero_2w)
	DTBS="bcm2710-rpi-zero-2-w.dtb"
	CONFIG="${HERE}/config.txt"
	;;
rpi_zero_w)
	DTBS="bcm2708-rpi-zero-w.dtb bcm2708-rpi-zero.dtb"
	CONFIG="${HERE}/config-rpi_zero_w.txt"
	;;
*)
	echo "error: unknown board '${BOARD}' (rpi_zero_2w | rpi_zero_w)" >&2
	exit 1
	;;
esac

OUT="pizza-${BOARD}.img"
SIZE_MB=256
PAYLOAD=()
ENTRY_NAMES=()
ENTRY_PATHS=()
while [ $# -gt 0 ]; do
	case "$1" in
	-o) OUT="$2"; shift 2 ;;
	-s) SIZE_MB="$2"; shift 2 ;;
	*=*)
		if [ "${MENU_MODE}" = 1 ]; then
			ENTRY_NAMES+=("${1%%=*}")
			ENTRY_PATHS+=("${1#*=}")
		else
			PAYLOAD+=("$1")
		fi
		shift ;;
	*) PAYLOAD+=("$1"); shift ;;
	esac
done

# Stage inputs where the container can see them.
WORK="${HERE}/.sdcard-work"
rm -rf "${WORK}" && mkdir -p "${WORK}" "${HERE}/blobs"

if [ "${MENU_MODE}" = 1 ]; then
	[ "${#ENTRY_NAMES[@]}" -ge 1 ] || {
		echo "error: --menu needs at least one Name=path entry" >&2
		exit 1
	}
	[ -f "${BOOTMENU_BIN}" ] || {
		echo "error: not found: ${BOOTMENU_BIN}" >&2
		exit 1
	}
	cp "${BOOTMENU_BIN}" "${WORK}/bootmenu.bin"

	# Every entry is a list entry; the first one is the default.
	DEFAULT_NAME="${ENTRY_NAMES[0]}"

	{
		echo "timeout = 5"
		echo "default = ${DEFAULT_NAME}"
		echo
	} > "${WORK}/menu.txt"

	i=0
	for name in "${ENTRY_NAMES[@]}"; do
		path="${ENTRY_PATHS[$i]}"
		[ -f "${path}" ] || { echo "error: not found: ${path}" >&2; exit 1; }
		lc="$(printf '%s' "${name}" | tr '[:upper:]' '[:lower:]')"
		slug="$(printf '%s' "${lc}" | tr -cd 'a-z0-9')"
		[ -n "${slug}" ] || {
			echo "error: entry name '${name}' has no usable characters" >&2
			exit 1
		}
		[ ! -e "${WORK}/${slug}.bin" ] || {
			echo "error: duplicate kernel filename ${slug}.bin (entry '${name}')" >&2
			exit 1
		}
		cp "${path}" "${WORK}/${slug}.bin"
		echo "${name} = ${slug}.bin" >> "${WORK}/menu.txt"
		i=$((i + 1))
	done

	# Per-board config template with the kernel= line replaced by the
	# WO-T1 selector block (default = menu, include = persisted choice,
	# button held = menu regardless).
	awk '/^kernel=/{
		print "gpio=17=ip,pu"
		print "kernel=bootmenu.bin"
		print "include chosen.txt"
		print ""
		print "[gpio17=0]"
		print "kernel=bootmenu.bin"
		print "[all]"
		next
	} {print}' "${CONFIG}" > "${WORK}/config.txt"
else
	[ -f "${ZEPHYR_BIN}" ] || { echo "error: not found: ${ZEPHYR_BIN}" >&2; exit 1; }
	cp "${ZEPHYR_BIN}" "${WORK}/zephyr.bin"
	cp "${CONFIG}" "${WORK}/config.txt"
fi
for f in "${PAYLOAD[@]+"${PAYLOAD[@]}"}"; do
	[ -f "$f" ] || { echo "error: payload not found: $f" >&2; exit 1; }
	cp "$f" "${WORK}/"
done

export FW_PIN SIZE_MB OUT DTBS
docker run --rm -e FW_PIN -e SIZE_MB -e OUT -e DTBS \
	-v "${HERE}/blobs:/blobs" -v "${WORK}:/work" -w /work ubuntu:22.04 bash -euc '
	export DEBIAN_FRONTEND=noninteractive
	apt-get update -qq >/dev/null
	apt-get install -y -qq mtools dosfstools fdisk curl ca-certificates >/dev/null

	# Pi boot blobs (cached in blobs/ between runs).
	for f in bootcode.bin start.elf fixup.dat $DTBS ; do
		[ -s "/blobs/$f" ] || curl -fsSL -o "/blobs/$f" \
			"https://raw.githubusercontent.com/raspberrypi/firmware/${FW_PIN}/boot/${f}"
	done

	# Blank image + a single FAT32 (LBA) partition starting at sector 2048.
	rm -f "$OUT"
	dd if=/dev/zero of="$OUT" bs=1M count="$SIZE_MB" status=none
	printf "label: dos\nstart=2048, type=c\n" | sfdisk "$OUT" >/dev/null

	# Format + populate the partition via mtools (offset = 2048 * 512).
	OFF=$((2048 * 512))
	mformat -i "${OUT}@@${OFF}" -F -v PIZZA ::
	DTB_PATHS=""
	for f in $DTBS ; do DTB_PATHS="${DTB_PATHS} /blobs/$f" ; done
	mcopy -i "${OUT}@@${OFF}" /blobs/bootcode.bin /blobs/start.elf /blobs/fixup.dat \
		$DTB_PATHS config.txt ::
	for f in *; do
		case "$f" in "$OUT"|config.txt) continue ;; esac
		mcopy -i "${OUT}@@${OFF}" "$f" "::$f"
	done

	echo "=== image contents ==="
	mdir -i "${OUT}@@${OFF}" ::
'

mv "${WORK}/${OUT}" "${HERE}/${OUT}"
rm -rf "${WORK}"
echo
echo "Built: ${HERE}/${OUT}"
echo "Flash it with Raspberry Pi Imager (Use custom) or balenaEtcher, then boot the Pi."
