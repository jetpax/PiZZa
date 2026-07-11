#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
#
# PiZZa sokoban asset packer.
#
# Walks the vendored game/data tree and emits a single binary pack
# linked into the Zephyr app (src/sok_pack.S):
#
#   *.png  ->  PIMG raw blobs (same format as SDLPoP's packer; the
#              shim's shim_image_pimg.c parses them -- no PNG decoder
#              on the device)
#   *.tmx  ->  S2TM baked map records (XML + base64/zlib parsed here;
#              the app's shim_tmx.c walks the record)
#   *.ttf  ->  skipped; fonts enter as baked S2SA glyph atlases via
#              --font "<path>@<size>" (the shim's shim_ttf_atlas.c +
#              include/sdl2shim_atlas.h define the contract)
#   else   ->  stored verbatim
#
# Pack layout (LE) -- SDLPoP's POPPACK1 with a sokoban magic and keys
# relative to the data root (no "data/" prefix):
#   u8  magic[8]  "SOKPACK1"
#   u32 nentries
#   entry[n]: u32 path_off, u32 data_off, u32 size
#   path strings (NUL-terminated, sorted by strcmp for bsearch)
#   blob data (8-byte aligned each)

import argparse
import base64
import struct
import sys
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

EXCLUDE_FILES = {".DS_Store"}


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


def bake_tmx(path: Path, data_root: Path) -> bytes:
    root = ET.parse(path).getroot()

    width = int(root.get("width"))
    height = int(root.get("height"))
    tile_w = int(root.get("tilewidth"))
    tile_h = int(root.get("tileheight"))
    bg = root.get("backgroundcolor", "#000000").lstrip("#")
    backgroundcolor = int(bg[-6:], 16)

    tilesets = root.findall("tileset")
    if len(tilesets) != 1:
        raise SystemExit(f"{path}: need exactly 1 tileset, found {len(tilesets)}")
    ts = tilesets[0]
    if ts.get("source"):
        raise SystemExit(f"{path}: external .tsx tilesets unsupported")
    firstgid = int(ts.get("firstgid"))
    tile_count = int(ts.get("tilecount"))
    image = ts.find("image")
    img_w = int(image.get("width"))
    columns = int(ts.get("columns", img_w // tile_w))

    # resolve the tileset image to its pack key (relative to data root)
    img_rel = (path.parent / image.get("source")).resolve().relative_to(
        data_root.resolve())
    img_key = "/".join(img_rel.parts)
    if len(img_key.encode()) >= 64:
        raise SystemExit(f"{path}: image key too long: {img_key}")

    layers = root.findall("layer")
    if len(layers) != 1:
        raise SystemExit(f"{path}: need exactly 1 tile layer, found {len(layers)}")
    data = layers[0].find("data")
    if data.get("encoding") != "base64" or data.get("compression") != "zlib":
        raise SystemExit(f"{path}: tile layer must be base64+zlib "
                         f"(got {data.get('encoding')}+{data.get('compression')})")
    gids = zlib.decompress(base64.b64decode(data.text.strip()))
    if len(gids) != width * height * 4:
        raise SystemExit(f"{path}: gid grid size mismatch")

    objects = []
    for og in root.findall("objectgroup"):
        for obj in og:
            if obj.get("visible", "1") == "0":
                continue
            gid = obj.get("gid")
            if gid is None:
                raise SystemExit(f"{path}: non-tile object id={obj.get('id')} "
                                 "unsupported")
            objects.append((int(gid) & 0x1FFFFFFF,
                            int(float(obj.get("x"))),
                            int(float(obj.get("y"))),
                            int(float(obj.get("width", tile_w))),
                            int(float(obj.get("height", tile_h)))))

    out = struct.pack("<4sHHHHIHHHH64s", b"S2TM", width, height, tile_w, tile_h,
                      backgroundcolor, len(objects), firstgid, tile_count,
                      columns, img_key.encode())
    out += gids
    for o in objects:
        out += struct.pack("<Iiiii", *o)
    return out


def bake_font(ttf_path: Path, size: int) -> bytes:
    font = ImageFont.truetype(str(ttf_path), size)
    ascent, descent = font.getmetrics()

    FIRST_CP, NCP = 32, 95          # printable ASCII
    ATLAS_W = 256

    glyphs = []
    bitmaps = []
    for cp in range(FIRST_CP, FIRST_CP + NCP):
        ch = chr(cp)
        advance = round(font.getlength(ch))
        bbox = font.getbbox(ch)     # (x0, y0, x1, y1), origin = top of line
        if bbox is None or bbox[2] <= bbox[0] or bbox[3] <= bbox[1]:
            glyphs.append((0, 0, 0, 0, 0, 0, advance))
            bitmaps.append(None)
            continue
        x0, y0, x1, y1 = bbox
        img = Image.new("L", (x1, y1), 0)
        ImageDraw.Draw(img).text((0, 0), ch, font=font, fill=255)
        glyph = img.crop(bbox)
        glyphs.append((glyph.width, glyph.height, x0, y0, advance))
        bitmaps.append(glyph)

    # shelf-pack into the atlas
    placed = []
    pen_x, pen_y, row_h = 0, 0, 0
    for i, bm in enumerate(bitmaps):
        if bm is None:
            placed.append((0, 0))
            continue
        if pen_x + bm.width > ATLAS_W:
            pen_y += row_h
            pen_x, row_h = 0, 0
        placed.append((pen_x, pen_y))
        pen_x += bm.width
        row_h = max(row_h, bm.height)
    atlas_h = pen_y + row_h

    atlas = bytearray(ATLAS_W * atlas_h)
    for (x, y), bm in zip(placed, bitmaps):
        if bm is None:
            continue
        data = bm.tobytes()
        for gy in range(bm.height):
            off = (y + gy) * ATLAS_W + x
            atlas[off:off + bm.width] = data[gy * bm.width:(gy + 1) * bm.width]

    out = struct.pack("<4sHHhhHHHH", b"S2SA", FIRST_CP, NCP, ascent, descent,
                      ATLAS_W, atlas_h, size, 0)
    for (x, y), g in zip(placed, glyphs):
        if len(g) == 7:             # empty glyph (w,h,xoff,yoff zeros, advance)
            w = h = xoff = yoff = 0
            advance = g[6]
        else:
            w, h, xoff, yoff, advance = g
        out += struct.pack("<HHBBbbbB", x, y, w, h, xoff, yoff, advance, 0)
    out += bytes(atlas)
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("data_dir", type=Path, help="vendored game data/ directory")
    ap.add_argument("output", type=Path, help="output pack file")
    ap.add_argument("--font", action="append", default=[],
                    metavar="PATH@SIZE",
                    help="bake a glyph atlas for data-relative font PATH at "
                         "SIZE px; stored under the key 'PATH@SIZE'")
    args = ap.parse_args()

    entries = {}
    stats = {"pimg": 0, "s2tm": 0, "raw": 0}

    for p in sorted(args.data_dir.rglob("*")):
        if not p.is_file() or p.name in EXCLUDE_FILES:
            continue
        key = "/".join(p.relative_to(args.data_dir).parts)
        suffix = p.suffix.lower()
        if suffix == ".png":
            entries[key] = convert_png(p)
            stats["pimg"] += 1
        elif suffix == ".tmx":
            entries[key] = bake_tmx(p, args.data_dir)
            stats["s2tm"] += 1
        elif suffix == ".ttf":
            continue                # atlases only, via --font
        else:
            entries[key] = p.read_bytes()
            stats["raw"] += 1

    for spec in args.font:
        rel, size = spec.rsplit("@", 1)
        ttf = args.data_dir / rel
        if not ttf.is_file():
            raise SystemExit(f"--font: {ttf} not found")
        entries[spec] = bake_font(ttf, int(size))
        print(f"[pack_assets] atlas {spec}: {len(entries[spec])} bytes")

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
    out += b"SOKPACK1"
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
    print(f"[pack_assets] {len(paths)} entries "
          f"({stats['pimg']} pimg, {stats['s2tm']} s2tm, {stats['raw']} raw, "
          f"{len(args.font)} atlas), pack {len(out)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
