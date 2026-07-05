#!/usr/bin/env bash
#
# SPDX-License-Identifier: Apache-2.0
#
# Build a flashable single-FAT32 SD image for the PiZZA Arduino loader.
#
# The Pi Zero 2 W boots a Zephyr kernel straight from a FAT boot partition, so
# the image is just: the Raspberry Pi boot blobs + config.txt + the loader
# (zephyr.bin, renamed). Flash the result with Raspberry Pi Imager / balenaEtcher,
# boot the Pi, then upload sketches over USB-CDC from the Arduino IDE -- no
# Raspberry Pi OS install, no manual file copying.
#
# Loop-free (uses mtools), so it runs in a plain Docker container -- no root,
# no privileged mode, works the same on macOS and Linux.
#
# Usage:
#   ./make-image.sh [path-to-zephyr.bin]
# Default loader: ../../../ArduinoCore-zephyr/firmwares/zephyr-rpi_zero_2w_bcm2710.bin
# (run PiZZa/apps/Arduino/dist-build.sh first to produce it).

set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

# raspberrypi/firmware commit the boot blobs are pinned to (reproducible builds).
export FW_PIN="afc8dbd74865c6d367dba5505c5a863252588ca8"
export SIZE_MB=256
export OUT="pizza-loader-rpi_zero_2w.img"

ZEPHYR_BIN="${1:-${HERE}/../../../../ArduinoCore-zephyr/firmwares/zephyr-rpi_zero_2w_bcm2710.bin}"
if [ ! -f "${ZEPHYR_BIN}" ]; then
	echo "error: loader not found: ${ZEPHYR_BIN}" >&2
	echo "       run PiZZa/apps/Arduino/dist-build.sh first, or pass the path." >&2
	exit 1
fi
cp "${ZEPHYR_BIN}" "${HERE}/zephyr.bin"

docker run --rm -e FW_PIN -e SIZE_MB -e OUT -v "${HERE}:/work" -w /work ubuntu:22.04 bash -euc '
	export DEBIAN_FRONTEND=noninteractive
	apt-get update -qq >/dev/null
	apt-get install -y -qq mtools dosfstools fdisk curl ca-certificates >/dev/null

	# Pi boot blobs (cached in blobs/ between runs).
	mkdir -p blobs
	for f in bootcode.bin start.elf fixup.dat bcm2710-rpi-zero-2-w.dtb ; do
		[ -s "blobs/$f" ] || curl -fsSL -o "blobs/$f" \
			"https://raw.githubusercontent.com/raspberrypi/firmware/${FW_PIN}/boot/${f}"
	done

	# Blank image + a single FAT32 (LBA) partition starting at sector 2048.
	rm -f "$OUT"
	dd if=/dev/zero of="$OUT" bs=1M count="$SIZE_MB" status=none
	printf "label: dos\nstart=2048, type=c\n" | sfdisk "$OUT" >/dev/null

	# Format + populate the partition via mtools (offset = 2048 * 512).
	OFF=$((2048 * 512))
	mformat -i "${OUT}@@${OFF}" -F -v PIZZA ::
	mcopy -i "${OUT}@@${OFF}" blobs/bootcode.bin blobs/start.elf blobs/fixup.dat \
		blobs/bcm2710-rpi-zero-2-w.dtb config.txt ::
	mcopy -i "${OUT}@@${OFF}" zephyr.bin ::zephyr.bin

	echo "=== image contents ==="
	mdir -i "${OUT}@@${OFF}" ::
'

rm -f "${HERE}/zephyr.bin"
echo
echo "Built: ${HERE}/${OUT}"
echo "Flash it with Raspberry Pi Imager (Use custom) or balenaEtcher, then boot the Pi."
