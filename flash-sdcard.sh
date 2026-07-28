#!/usr/bin/env bash
#
# SPDX-License-Identifier: Apache-2.0
#
# Write a PiZZa SD image to a card -- the scripted equivalent of
# clicking through Raspberry Pi Imager, with guards so it cannot
# quietly eat a backup drive.
#
# Takes a .img or a .img.xz (decompressed on the fly, nothing is
# unpacked to disk), writes it to the raw character device, reads the
# same number of bytes back, compares SHA-256, and ejects.
#
# Usage:
#   ./flash-sdcard.sh <image.img|image.img.xz> [device]
#
# With no device, the removable candidates are listed and nothing is
# written. The device is a whole disk -- /dev/disk6 on macOS,
# /dev/sdb or /dev/mmcblk0 on Linux -- never a partition.
#
# Options:
#   --authopen        macOS: authorise via a GUI dialog instead of sudo
#   --max-size-gb N   raise the size ceiling (default 128)
#   --no-verify       skip the read-back compare
#   --yes             skip the typed confirmation (for scripting)
#
# Guards, all of which must pass:
#   - the target is a whole disk, not a partition
#   - the target is external / removable
#   - the target is not the disk currently backing /
#   - the target is at or below the size ceiling; the default of 128 GB
#     rejects the desktop backup drives that otherwise look exactly
#     like a card (external, ejectable, whole disk)
#   - you retype the disk identifier, having been shown every volume
#     on it
#
# Examples:
#   ./flash-sdcard.sh pizza-menu-rpi_zero_2w-v0.7.0.img.xz
#   ./flash-sdcard.sh pizza-menu-rpi_zero_2w-v0.7.0.img.xz /dev/disk6
#
# To swap only the Zephyr app on a card that is already imaged, use
# install-to-sdcard.sh instead -- that is a file copy and needs no
# privileges at all.

set -euo pipefail

MAX_SIZE_GB=128
USE_AUTHOPEN=0
VERIFY=1
ASSUME_YES=0
IMAGE=""
DEVICE=""

die() {
	echo "error: $*" >&2
	exit 1
}

while [ $# -gt 0 ]; do
	case "$1" in
	--authopen) USE_AUTHOPEN=1; shift ;;
	--max-size-gb) MAX_SIZE_GB="${2:?--max-size-gb needs a number}"; shift 2 ;;
	--no-verify) VERIFY=0; shift ;;
	--yes) ASSUME_YES=1; shift ;;
	-h|--help) sed -n '3,45p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
	-*) die "unknown option $1" ;;
	*)
		if [ -z "${IMAGE}" ]; then
			IMAGE="$1"
		elif [ -z "${DEVICE}" ]; then
			DEVICE="$1"
		else
			die "unexpected argument $1"
		fi
		shift
		;;
	esac
done

[ -n "${IMAGE}" ] || die "usage: $0 <image.img|image.img.xz> [device]"
[ -f "${IMAGE}" ] || die "not found: ${IMAGE}"

OS="$(uname -s)"

# Uncompressed byte count -- what gets written, and what gets read back
# for the verify.
case "${IMAGE}" in
*.xz)
	command -v xz >/dev/null || die "xz not found (brew install xz)"
	IMAGE_SIZE="$(xz --robot --list "${IMAGE}" | awk '$1 == "file" { print $5 }')"
	DECOMP=(xz -dc "${IMAGE}")
	;;
*)
	case "${OS}" in
	Darwin) IMAGE_SIZE="$(stat -f%z "${IMAGE}")" ;;
	*)      IMAGE_SIZE="$(stat -c%s "${IMAGE}")" ;;
	esac
	DECOMP=(cat "${IMAGE}")
	;;
esac
[ -n "${IMAGE_SIZE}" ] && [ "${IMAGE_SIZE}" -gt 0 ] || die "could not size ${IMAGE}"

list_candidates_darwin() {
	echo "Removable candidates:"
	echo
	diskutil list external physical
	echo "Re-run with the disk identifier, e.g.: $0 ${IMAGE} /dev/disk6"
}

list_candidates_linux() {
	echo "Removable candidates:"
	echo
	lsblk -dno NAME,SIZE,RM,TYPE,MODEL | awk '$3 == 1 && $4 == "disk"'
	echo
	echo "Re-run with the device, e.g.: $0 ${IMAGE} /dev/sdb"
}

if [ -z "${DEVICE}" ]; then
	case "${OS}" in
	Darwin) list_candidates_darwin ;;
	Linux)  list_candidates_linux ;;
	*)      die "unsupported OS: ${OS}" ;;
	esac
	exit 1
fi

[ -e "${DEVICE}" ] || die "no such device: ${DEVICE}"

DEV_NAME="$(basename "${DEVICE}")"
DEV_NAME="${DEV_NAME#r}"

# --- guards -----------------------------------------------------------

if [ "${OS}" = "Darwin" ]; then
	INFO="$(diskutil info -plist "/dev/${DEV_NAME}" 2>/dev/null)" ||
		die "diskutil does not recognise ${DEVICE}"

	plist_get() {
		printf '%s' "${INFO}" | plutil -extract "$1" raw - 2>/dev/null || echo ""
	}

	[ "$(plist_get WholeDisk)" = "true" ] ||
		die "${DEVICE} is a partition; pass the whole disk (e.g. /dev/$(printf '%s' "${DEV_NAME}" | sed 's/s[0-9]*$//'))"
	[ "$(plist_get Internal)" = "false" ] ||
		die "${DEVICE} is an internal disk"

	DEV_SIZE="$(plist_get TotalSize)"
	DEV_DESC="$(plist_get MediaName) ($(plist_get BusProtocol))"

	ROOT_DISK="$(diskutil info -plist / 2>/dev/null |
		plutil -extract ParentWholeDisk raw - 2>/dev/null || echo "")"
	[ "${DEV_NAME}" != "${ROOT_DISK}" ] ||
		die "${DEVICE} is the disk backing / -- refusing"

	RAW_DEV="/dev/r${DEV_NAME}"
else
	[ -b "${DEVICE}" ] || die "${DEVICE} is not a block device"

	SYS="/sys/class/block/${DEV_NAME}"
	[ -d "${SYS}" ] || die "${DEVICE} has no sysfs entry"
	[ -e "${SYS}/device" ] ||
		die "${DEVICE} looks like a partition; pass the whole disk"
	[ "$(cat "${SYS}/removable" 2>/dev/null || echo 0)" = "1" ] ||
		die "${DEVICE} is not removable"

	DEV_SIZE=$(( $(cat "${SYS}/size") * 512 ))
	DEV_DESC="$(cat "${SYS}/device/model" 2>/dev/null || echo "unknown")"

	ROOT_SRC="$(findmnt -no SOURCE / || echo "")"
	case "${ROOT_SRC}" in
	*"${DEV_NAME}"*) die "${DEVICE} carries / -- refusing" ;;
	esac

	RAW_DEV="${DEVICE}"
fi

[ -n "${DEV_SIZE}" ] && [ "${DEV_SIZE}" -gt 0 ] || die "could not size ${DEVICE}"

MAX_BYTES=$(( MAX_SIZE_GB * 1000 * 1000 * 1000 ))
if [ "${DEV_SIZE}" -gt "${MAX_BYTES}" ]; then
	echo "error: ${DEVICE} is $(( DEV_SIZE / 1000000000 )) GB, over the" >&2
	echo "       ${MAX_SIZE_GB} GB ceiling. Backup drives look just like" >&2
	echo "       cards to every other check here. If this really is the" >&2
	echo "       target, re-run with --max-size-gb $(( DEV_SIZE / 1000000000 + 1 ))." >&2
	exit 1
fi

if [ "${IMAGE_SIZE}" -gt "${DEV_SIZE}" ]; then
	die "image is $(( IMAGE_SIZE / 1000000 )) MB, card is only $(( DEV_SIZE / 1000000 )) MB"
fi

# --- confirm ----------------------------------------------------------

echo
echo "  image  : ${IMAGE}"
echo "           $(( IMAGE_SIZE / 1000000 )) MB written"
echo "  target : ${DEVICE}  --  ${DEV_DESC}"
echo "           $(( DEV_SIZE / 1000000000 )) GB, everything on it is destroyed"
echo

if [ "${OS}" = "Darwin" ]; then
	diskutil list "/dev/${DEV_NAME}"
else
	lsblk "${DEVICE}"
fi
echo

if [ "${ASSUME_YES}" != 1 ]; then
	printf 'Type the disk identifier (%s) to proceed: ' "${DEV_NAME}"
	read -r reply
	[ "${reply}" = "${DEV_NAME}" ] || die "not confirmed (got '${reply}')"
fi

# --- write ------------------------------------------------------------

if [ "${OS}" = "Darwin" ]; then
	diskutil unmountDisk "/dev/${DEV_NAME}"
else
	for part in $(lsblk -lno NAME "${DEVICE}" | tail -n +2); do
		mountpoint -q "/dev/${part}" 2>/dev/null && umount "/dev/${part}" || true
	done
fi

echo "Writing to ${RAW_DEV} (Ctrl-T for progress)..."

if [ "${OS}" = "Darwin" ] && [ "${USE_AUTHOPEN}" = 1 ]; then
	"${DECOMP[@]}" | /usr/libexec/authopen -w -c "${RAW_DEV}"
elif [ "$(id -u)" = "0" ]; then
	"${DECOMP[@]}" | dd of="${RAW_DEV}" bs=4m 2>/dev/null ||
		"${DECOMP[@]}" | dd of="${RAW_DEV}" bs=4M
else
	"${DECOMP[@]}" | sudo dd of="${RAW_DEV}" bs=4m 2>/dev/null ||
		"${DECOMP[@]}" | sudo dd of="${RAW_DEV}" bs=4M
fi

sync

# --- verify -----------------------------------------------------------

if [ "${VERIFY}" = 1 ]; then
	echo "Verifying ${IMAGE_SIZE} bytes..."

	SHA_CMD="shasum -a 256"
	command -v sha256sum >/dev/null && SHA_CMD="sha256sum"

	WANT="$("${DECOMP[@]}" | ${SHA_CMD} | cut -d' ' -f1)"

	read_back() {
		if [ "${OS}" = "Darwin" ] && [ "${USE_AUTHOPEN}" = 1 ]; then
			/usr/libexec/authopen "${RAW_DEV}"
		elif [ "$(id -u)" = "0" ]; then
			dd if="${RAW_DEV}" bs=1m 2>/dev/null
		else
			sudo dd if="${RAW_DEV}" bs=1m 2>/dev/null
		fi
	}

	GOT="$(read_back | head -c "${IMAGE_SIZE}" | ${SHA_CMD} | cut -d' ' -f1)"

	if [ "${WANT}" != "${GOT}" ]; then
		echo "error: read-back mismatch -- the card did not take the image" >&2
		echo "       expected ${WANT}" >&2
		echo "       got      ${GOT}" >&2
		exit 1
	fi
	echo "Verified: ${WANT}"
fi

# --- eject ------------------------------------------------------------

if [ "${OS}" = "Darwin" ]; then
	diskutil eject "/dev/${DEV_NAME}"
else
	udisksctl power-off -b "${DEVICE}" 2>/dev/null || true
fi

echo
echo "Done. Put the card in the Pi."
