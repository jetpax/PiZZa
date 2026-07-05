#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
#
# PiZZa CDC-loader spike — host uploader.
# Sends a sketch.llext to the board over USB CDC:  "PZUP" + le32 len + bytes.
#
#   ~/.zephyr-venv/bin/python upload.py /dev/cu.usbmodemXXXX sketch.llext
#
# (pyserial is already in the zephyr venv.)

import struct
import sys
import time

import serial


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: upload.py <serial-port> <sketch.llext>")

    port, path = sys.argv[1], sys.argv[2]
    with open(path, "rb") as fh:
        data = fh.read()

    # 1) 1200-bps touch: the IDE's standard "enter upload mode" signal.
    #    Open at 1200 baud, hold long enough for the firmware to poll it,
    #    then close. The firmware sees baud==1200 and switches to upload mode.
    print("1200-bps touch...")
    t = serial.Serial(port, 1200)
    time.sleep(0.6)
    t.close()
    time.sleep(0.4)            # let the firmware enter upload-listen

    # 2) Reopen and send the sketch: "PZUP" + le32 length + bytes.
    s = serial.Serial(port, 115200, timeout=8)
    time.sleep(0.2)
    s.reset_input_buffer()
    s.write(b"PZUP" + struct.pack("<I", len(data)) + data)
    s.flush()
    print(f"sent {len(data)} bytes to {port}; waiting for device...")

    line = s.readline().decode(errors="replace").strip()
    print("device:", line if line else "(no response)")
    s.close()
    sys.exit(0 if line == "OK" else 1)


if __name__ == "__main__":
    main()
