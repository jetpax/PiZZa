#!/usr/bin/env bash
#
# SPDX-License-Identifier: Apache-2.0
#
# Build a flashable single-FAT32 SD image for any PiZZa Zephyr app --
# the same way the Arduino loader images are built (and unlike them,
# board-parameterised). The Pi boots a Zephyr kernel straight from a
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
#   ./make-sdcard.sh <rpi_zero_2w|rpi_zero_w> <zephyr.bin> [-o NAME.img]
#                    [-s SIZE_MB] [payload files...]
#
# Examples:
#   ./make-sdcard.sh rpi_zero_2w ~/zephyrproject/build/pizzashell/zephyr/zephyr.bin
#   ./make-sdcard.sh rpi_zero_2w build/doom/zephyr/zephyr.bin -o pizza-doom.img doom.img

set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

# raspberrypi/firmware commit the boot blobs are pinned to (reproducible
# builds; same pin as apps/Arduino/loader-image).
FW_PIN="afc8dbd74865c6d367dba5505c5a863252588ca8"

BOARD="${1:?usage: $0 <rpi_zero_2w|rpi_zero_w> <zephyr.bin> [-o out.img] [-s size_mb] [payload...]}"
ZEPHYR_BIN="${2:?path to zephyr.bin required}"
shift 2

case "${BOARD}" in
rpi_zero_2w)
	DTB="bcm2710-rpi-zero-2-w.dtb"
	CONFIG="${HERE}/config.txt"
	;;
rpi_zero_w)
	DTB="bcm2708-rpi-zero-w.dtb"
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
while [ $# -gt 0 ]; do
	case "$1" in
	-o) OUT="$2"; shift 2 ;;
	-s) SIZE_MB="$2"; shift 2 ;;
	*) PAYLOAD+=("$1"); shift ;;
	esac
done

[ -f "${ZEPHYR_BIN}" ] || { echo "error: not found: ${ZEPHYR_BIN}" >&2; exit 1; }

# Stage inputs where the container can see them.
WORK="${HERE}/.sdcard-work"
rm -rf "${WORK}" && mkdir -p "${WORK}" "${HERE}/blobs"
cp "${ZEPHYR_BIN}" "${WORK}/zephyr.bin"
cp "${CONFIG}" "${WORK}/config.txt"
for f in "${PAYLOAD[@]+"${PAYLOAD[@]}"}"; do
	[ -f "$f" ] || { echo "error: payload not found: $f" >&2; exit 1; }
	cp "$f" "${WORK}/"
done

export FW_PIN SIZE_MB OUT DTB
docker run --rm -e FW_PIN -e SIZE_MB -e OUT -e DTB \
	-v "${HERE}/blobs:/blobs" -v "${WORK}:/work" -w /work ubuntu:22.04 bash -euc '
	export DEBIAN_FRONTEND=noninteractive
	apt-get update -qq >/dev/null
	apt-get install -y -qq mtools dosfstools fdisk curl ca-certificates >/dev/null

	# Pi boot blobs (cached in blobs/ between runs).
	for f in bootcode.bin start.elf fixup.dat "$DTB" ; do
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
	mcopy -i "${OUT}@@${OFF}" /blobs/bootcode.bin /blobs/start.elf /blobs/fixup.dat \
		"/blobs/$DTB" config.txt ::
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
