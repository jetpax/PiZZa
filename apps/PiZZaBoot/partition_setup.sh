#!/bin/sh
# partition_setup.sh -- runs on the Pi during PINN silentinstall, as root.
#
# Called by PINN's multiimagewritethread after extracting the PiZZaBoot
# tarball into our boot partition. Arguments are positional `partN=PATH`
# pairs naming the partitions PINN has just provisioned for this OS, e.g.:
#
#     partition_setup.sh part1=/dev/mmcblk0p6 id1=LABEL=pizzaboot
#
# Our job here is to make subsequent cold boots land in PiZZaBoot instead
# of PINN's interactive selector. We do that by dropping an `autoboot.txt`
# into PINN's RECOVERY partition (the first FAT partition; the firmware's
# boot partition). PINN reads autoboot.txt at start-up and immediately
# chainboots the partition it names. The Stage-4 "overwrite p1" variant
# from boot_manager.html §3 is the eventual goal; autoboot is the safer
# v1 mechanism (no self-modification of the running PINN partition).

set -e

# --- Parse args ---
for arg in "$@"; do
    case "$arg" in
        part1=*) BOOT_PART="${arg#part1=}" ;;
    esac
done

if [ -z "$BOOT_PART" ]; then
    echo "partition_setup.sh: missing part1= argument" >&2
    exit 1
fi

# Extract the partition number from /dev/mmcblk0pN (or /dev/sdaN, etc.)
PART_NUM=$(echo "$BOOT_PART" | sed -E 's|.*[a-z]([0-9]+)$|\1|')
if [ -z "$PART_NUM" ]; then
    echo "partition_setup.sh: could not extract partition number from $BOOT_PART" >&2
    exit 1
fi

# --- Find PINN's RECOVERY partition (p1 by convention) ---
# PINN itself is currently running from /dev/mmcblk0p1. The same disk hosts
# our new boot partition, so derive p1 by replacing the trailing digits.
DISK_PREFIX=$(echo "$BOOT_PART" | sed -E 's|[0-9]+$||')
RECOVERY="${DISK_PREFIX}1"

MOUNTPOINT=/tmp/pinn-recovery
mkdir -p "$MOUNTPOINT"
mount "$RECOVERY" "$MOUNTPOINT"

# --- Drop autoboot.txt so subsequent cold boots skip PINN's GUI ---
echo "boot_partition=$PART_NUM" > "$MOUNTPOINT/autoboot.txt"

sync
umount "$MOUNTPOINT"
rmdir "$MOUNTPOINT"

echo "PiZZaBoot: autoboot.txt written, boot_partition=$PART_NUM"
