#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# install-to-sdcard.sh -- drop a Zephyr build onto a PiZZa-imaged
# microSD card (see make-sdcard.sh to create one).
#
# Usage:
#   install-to-sdcard.sh [rpi_zero_2w|rpi_zero_w] [--slot <name>] \
#                        [<boot-partition-mount>] <path-to-zephyr.bin>
#
# --slot is for multi-app boot-menu cards (make-sdcard.sh --menu): it
# names the menu.txt entry to replace, e.g. --slot RetroPiZZa or
# --slot shell. On such a card --slot is REQUIRED and config.txt is
# never rewritten; run without it to have the available slots listed.
#
# The board defaults to rpi_zero_2w. THE BOARDS ARE NOT INTERCHANGEABLE:
# the Zero 2 W boots a 64-bit kernel at 0x200000, the original Zero W a
# 32-bit kernel at 0x8000 -- installing with the wrong board writes a
# config.txt the kernel can't boot from (no console, no enumeration).
#
# Examples:
#   # macOS, PiZZa card auto-mounts at /Volumes/PIZZA:
#   ./install-to-sdcard.sh ~/Downloads/zephyr.bin
#
#   # Original Pi Zero W:
#   ./install-to-sdcard.sh rpi_zero_w ~/Downloads/zephyr.bin
#
#   # Explicit mount point:
#   ./install-to-sdcard.sh /Volumes/PIZZA ~/Downloads/zephyr.bin
#
#   # Linux:
#   ./install-to-sdcard.sh /media/$USER/PIZZA ~/Downloads/zephyr.bin
#
# The script:
#   1. Copies zephyr.bin into the boot partition.
#   2. Replaces config.txt with the board's boot params (from the
#      repo's config.txt / config-rpi_zero_w.txt), preserving the
#      original as config.txt.orig on first run.
#
# Pi firmware blobs (bootcode.bin, fixup.dat, start.elf) stay untouched.

set -e

HERE=$(cd "$(dirname "$0")" && pwd)

# Optional board selector as the first argument.
BOARD="rpi_zero_2w"
case "${1:-}" in
rpi_zero_2w|rpi_zero_w)
	BOARD="$1"
	shift
	;;
esac

# Optional slot selector, for multi-app boot-menu cards.
SLOT=""
if [ "${1:-}" = "--slot" ]; then
	SLOT="${2:?usage: --slot <menu entry name>}"
	shift 2
fi
case "$BOARD" in
rpi_zero_2w) CONFIG_SRC="$HERE/config.txt" ;;
rpi_zero_w)  CONFIG_SRC="$HERE/config-rpi_zero_w.txt" ;;
esac

# Default mount point if only one arg given (first existing candidate
# wins -- PIZZA is the make-sdcard.sh volume label, RECOVERY covers
# cards imaged before July 2026). Override by passing two args.
DEFAULT_MOUNT="/Volumes/PIZZA"
[ -d "$DEFAULT_MOUNT" ] || { [ -d /Volumes/RECOVERY ] && DEFAULT_MOUNT="/Volumes/RECOVERY"; } || true

case $# in
1)
	MOUNT="$DEFAULT_MOUNT"
	ZEPHYR_BIN="$1"
	;;
2)
	MOUNT="$1"
	ZEPHYR_BIN="$2"
	;;
*)
	cat >&2 <<EOF
Usage: $0 [<recovery-mount-point>] <path-to-zephyr.bin>

If <boot-partition-mount> is omitted it defaults to $DEFAULT_MOUNT
(the macOS auto-mount path for a PiZZa-imaged SD card).
EOF
	exit 1
	;;
esac

if [ ! -d "$MOUNT" ]; then
	echo "error: $MOUNT is not a directory (is the SD card mounted?)" >&2
	exit 1
fi
if [ ! -f "$ZEPHYR_BIN" ]; then
	echo "error: $ZEPHYR_BIN: file not found" >&2
	exit 1
fi
if [ ! -f "$MOUNT/bootcode.bin" ] || [ ! -f "$MOUNT/start.elf" ]; then
	echo "error: $MOUNT does not look like a Pi boot partition" >&2
	echo "       (no bootcode.bin or start.elf present)" >&2
	echo "       Flash a PiZZa image first -- see make-sdcard.sh / Releases." >&2
	exit 1
fi

# A multi-app boot-menu card (make-sdcard.sh --menu) stages PiZZaBoot as
# bootmenu.bin plus a menu.txt naming one kernel per entry, and its
# config.txt is the WO-T1 selector -- kernel=bootmenu.bin, include
# chosen.txt, and the [gpio17=0] force-menu block. Writing zephyr.bin and
# a single-kernel config.txt over that would silently retarget whichever
# entry happens to be called zephyr.bin AND destroy the selector, taking
# the whole menu with it. So on a menu card an explicit --slot is
# required and config.txt is left alone.
IS_MENU_CARD=0
if [ -f "$MOUNT/bootmenu.bin" ] && [ -f "$MOUNT/menu.txt" ]; then
	IS_MENU_CARD=1
fi

menu_slots() {
	awk -F= '/^[[:space:]]*[^#[:space:]]/ {
		key = $1
		gsub(/^[[:space:]]+|[[:space:]]+$/, "", key)
		if (key == "timeout" || key == "default") next
		print key
	}' "$MOUNT/menu.txt"
}

if [ "$IS_MENU_CARD" = 1 ] && [ -z "$SLOT" ]; then
	echo "error: $MOUNT is a boot-menu card; --slot <name> is required" >&2
	echo "       Installing without it would overwrite the wrong kernel" >&2
	echo "       and destroy the menu's config.txt. Available slots:" >&2
	menu_slots | sed 's/^/         /' >&2
	exit 1
fi
if [ "$IS_MENU_CARD" = 0 ] && [ -n "$SLOT" ]; then
	echo "error: --slot given but $MOUNT is not a boot-menu card" >&2
	echo "       (no bootmenu.bin / menu.txt present)" >&2
	exit 1
fi

if [ "$IS_MENU_CARD" = 1 ]; then
	TARGET=$(awk -F= -v want="$SLOT" '{
		key = $1; val = $2
		gsub(/^[[:space:]]+|[[:space:]]+$/, "", key)
		gsub(/^[[:space:]]+|[[:space:]]+$/, "", val)
		if (key == want) { print val; exit }
	}' "$MOUNT/menu.txt")
	if [ -z "$TARGET" ]; then
		echo "error: no menu entry named '$SLOT'. Available slots:" >&2
		menu_slots | sed 's/^/         /' >&2
		exit 1
	fi
	cp "$ZEPHYR_BIN" "$MOUNT/$TARGET"
	echo "Installed $(basename "$ZEPHYR_BIN") to $MOUNT/$TARGET (slot '$SLOT')."
	echo "config.txt and menu.txt left untouched."
	echo "Eject the card and boot the Pi:"
	echo
	case "$(uname -s)" in
	Darwin) echo "  diskutil eject \"$MOUNT\"" ;;
	Linux)  echo "  udisksctl unmount -b \"\$(findmnt -no SOURCE \"$MOUNT\")\"" ;;
	*)      echo "  (eject the card via your OS's usual mechanism)" ;;
	esac
	exit 0
fi

cp "$ZEPHYR_BIN" "$MOUNT/zephyr.bin"

if [ -f "$MOUNT/config.txt" ] && [ ! -f "$MOUNT/config.txt.orig" ]; then
	cp "$MOUNT/config.txt" "$MOUNT/config.txt.orig"
fi

{
	echo "# Generated by jetpax/PiZZa::install-to-sdcard.sh ($BOARD)."
	echo "# The previous config.txt is preserved as config.txt.orig."
	echo
	cat "$CONFIG_SRC"
} > "$MOUNT/config.txt"

echo "Installed $(basename "$ZEPHYR_BIN") to $MOUNT ($BOARD config)."
echo "Eject the card and boot the Pi:"
echo
case "$(uname -s)" in
Darwin)
	echo "  diskutil eject \"$MOUNT\""
	;;
Linux)
	echo "  udisksctl unmount -b \"\$(findmnt -no SOURCE \"$MOUNT\")\""
	;;
*)
	echo "  (eject the card via your OS's usual mechanism)"
	;;
esac
