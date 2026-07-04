#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
#
# PiZZa SDLPoP asset packer -- work-order §4 preferred pipeline.
#
# Walks SDLPoP's data/ tree (music excluded: audio is deferred scope)
# and emits a single binary pack linked into the Zephyr app:
#
#   res###.png / icon.png / light.png  ->  PIMG raw blobs
#       indexed PNG (927/929 of the tree)  -> 8bpp pixels + RGBA
#       palette (tRNS preserved); truecolor -> RGB24 / ARGB32 raw.
#       The device-side IMG_Load_RW parses PIMG with a header read --
#       no PNG decoder on the device.
#   everything else (.bin/.pal/.DAT/...)  ->  stored verbatim
#
# Pack layout (LE):
#   u8  magic[8]  "POPPACK1"
#   u32 nentries
#   entry[n]: u32 path_off, u32 data_off, u32 size
#   path strings (NUL-terminated, sorted by strcmp for bsearch)
#   blob data (8-byte aligned each)
#
# PIMG layout (LE):
#   u8 magic[4] "PIMG"; u16 w; u16 h; u8 kind (0=INDEX8, 1=RGB24,
#   2=ARGB32); u8 rsvd; u16 ncolors; RGBA palette[ncolors]; pixels
#   (INDEX8 w*h | RGB24 w*h*3 [R,G,B] | ARGB32 w*h*4 [B,G,R,A])

import argparse
import struct
import sys
from pathlib import Path

from PIL import Image

EXCLUDE_DIRS = {"music"}
EXCLUDE_FILES = {".DS_Store", "names.txt"}


def convert_png(path: Path) -> bytes:
    img = Image.open(path)

    if img.mode == "P":
        pixels = img.tobytes()
        pal = img.getpalette() or []
        ncolors = max(1, len(pal) // 3)
        trans = img.info.get("transparency")
        alpha = [255] * ncolors
        if isinstance(trans, int):
            if trans < ncolors:
                alpha[trans] = 0
        elif trans is not None:
            for i, a in enumerate(bytes(trans)[:ncolors]):
                alpha[i] = a
        palette = bytearray()
        for i in range(ncolors):
            r, g, b = pal[i * 3:i * 3 + 3] or (0, 0, 0)
            palette += bytes((r, g, b, alpha[i]))
        kind = 0
    elif img.mode in ("RGB", "L"):
        img = img.convert("RGB")
        pixels = img.tobytes()          # R,G,B byte order
        palette = bytearray()
        ncolors = 0
        kind = 1
    else:
        img = img.convert("RGBA")
        rgba = img.tobytes()
        px = bytearray(len(rgba))
        px[0::4] = rgba[2::4]           # B
        px[1::4] = rgba[1::4]           # G
        px[2::4] = rgba[0::4]           # R
        px[3::4] = rgba[3::4]           # A
        pixels = bytes(px)
        palette = bytearray()
        ncolors = 0
        kind = 2

    header = struct.pack("<4sHHBBH", b"PIMG", img.width, img.height, kind, 0, ncolors)
    return header + bytes(palette) + pixels


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("data_dir", type=Path, help="SDLPoP data/ directory")
    ap.add_argument("output", type=Path, help="output pack file")
    ap.add_argument("--overlay", type=Path, action="append", default=[],
                    help="extra dir whose files map to pack ROOT (e.g. a "
                         "tuning SDLPoP.ini); overrides data/ entries by key")
    args = ap.parse_args()

    entries = {}
    png_in = png_out = raw_bytes = 0

    for p in sorted(args.data_dir.rglob("*")):
        if not p.is_file() or p.name in EXCLUDE_FILES:
            continue
        rel = p.relative_to(args.data_dir)
        if rel.parts[0] in EXCLUDE_DIRS:
            continue
        key = "data/" + "/".join(rel.parts)
        if p.suffix.lower() == ".png":
            blob = convert_png(p)
            png_in += p.stat().st_size
            png_out += len(blob)
        else:
            blob = p.read_bytes()
            raw_bytes += len(blob)
        entries[key] = blob

    # Overlay files land at pack root ("SDLPoP.ini", not "data/..."),
    # so they satisfy the game's CWD-relative config lookups.
    for odir in args.overlay:
        for p in sorted(odir.rglob("*")):
            if not p.is_file() or p.name == ".DS_Store":
                continue
            key = "/".join(p.relative_to(odir).parts)
            entries[key] = p.read_bytes()
            raw_bytes += len(entries[key])
            print(f"[pack_assets] overlay: {key}")

    paths = sorted(entries)   # strcmp order == python sort for ASCII paths

    strtab = bytearray()
    path_offs = {}
    for k in paths:
        path_offs[k] = len(strtab)
        strtab += k.encode() + b"\0"

    header_len = 8 + 4 + 12 * len(paths)
    strtab_off = header_len
    data_off = (strtab_off + len(strtab) + 7) & ~7

    blob_offs = {}
    blobs = bytearray()
    for k in paths:
        blob_offs[k] = data_off + len(blobs)
        blobs += entries[k]
        pad = (-len(blobs)) % 8
        blobs += b"\0" * pad

    out = bytearray()
    out += b"POPPACK1"
    out += struct.pack("<I", len(paths))
    for k in paths:
        out += struct.pack("<III", strtab_off + path_offs[k], blob_offs[k],
                           len(entries[k]))
    out += strtab
    out += b"\0" * ((-len(out)) % 8)
    assert len(out) == data_off, (len(out), data_off)
    out += blobs

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(out)
    print(f"[pack_assets] {len(paths)} entries, "
          f"png {png_in} -> pimg {png_out} bytes, raw {raw_bytes} bytes, "
          f"pack {len(out)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
