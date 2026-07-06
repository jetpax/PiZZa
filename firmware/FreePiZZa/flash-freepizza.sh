#!/usr/bin/env bash
# =============================================================================
# flash-freepizza.sh — Copy Free PiZZa boot files to a FAT32 SD card
# =============================================================================
# Usage:
#   ./flash-freepizza.sh                         # -> /Volumes/FREEPIZZA
#   ./flash-freepizza.sh /Volumes/OTHER          # override mount
#   BIN=build/bootcode.G0a.bin ./flash-freepizza.sh   # override which bin
#
# Copies:
#   bootcode.bin  (from ${BIN:-build/bootcode.bin})
#   config.txt    (from sd-card/config.txt)
#   cmdline.txt   (from sd-card/cmdline.txt)
#
# Then syncs and — on macOS — ejects the disk so the card can be
# pulled without a nag dialog.
# =============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
MOUNT="${1:-/Volumes/FREEPIZZA}"
BIN="${BIN:-$HERE/build/bootcode.bin}"
SD_DIR="$HERE/sd-card"

# ---------------------------------------------------------------------------
# Validate inputs
# ---------------------------------------------------------------------------
if [[ ! -d "$MOUNT" ]]; then
	echo "error: mount point not found: $MOUNT" >&2
	echo "       insert card and check: ls /Volumes/" >&2
	exit 1
fi

if [[ ! -f "$BIN" ]]; then
	echo "error: bootcode.bin not found: $BIN" >&2
	echo "       expected at $HERE/build/bootcode.bin -- run docker build first" >&2
	exit 1
fi

# Refuse to write anywhere that looks like the system root / a home dir
case "$MOUNT" in
	/|/System*|/Users*|/private*|/tmp*|/var*|/etc*|/bin*|/sbin*|/usr*|/opt*|/Library*)
		echo "error: refusing to write to system path: $MOUNT" >&2
		exit 1 ;;
esac

# ---------------------------------------------------------------------------
# Summarise + confirm
# ---------------------------------------------------------------------------
BIN_SIZE=$(wc -c < "$BIN" | tr -d ' ')
echo "=== Flash Free PiZZa ==="
echo "  mount:    $MOUNT"
echo "  bootcode: $BIN  (${BIN_SIZE} bytes)"
echo "  config:   $SD_DIR/config.txt"
echo "  cmdline:  $SD_DIR/cmdline.txt"
echo

# ---------------------------------------------------------------------------
# Copy
# ---------------------------------------------------------------------------
cp "$BIN"                "$MOUNT/bootcode.bin"
cp "$SD_DIR/config.txt"  "$MOUNT/config.txt"
cp "$SD_DIR/cmdline.txt" "$MOUNT/cmdline.txt"

sync

echo "=== files on card ==="
ls -la "$MOUNT/bootcode.bin" "$MOUNT/config.txt" "$MOUNT/cmdline.txt"

# ---------------------------------------------------------------------------
# Eject (macOS)
# ---------------------------------------------------------------------------
if command -v diskutil >/dev/null 2>&1; then
	echo
	echo "ejecting $MOUNT ..."
	diskutil eject "$MOUNT"
fi

echo
echo "done — insert card into Pi Zero 2 W, apply USB power, watch UART @115200."
