#!/usr/bin/env python3
"""
flip_bmp.py -- invert the pixels of the 1-bit BMPs used as sleep-screen
backgrounds by the ESP32 WAV player (Sleep.bmp / DeepSleep.bmp).

WHY IT FLIPS THE PIXELS AND NOT THE PALETTE
-------------------------------------------
A 1-bpp BMP stores a 2-entry palette, and the firmware's loadScreenBmp() READS
that palette to decide what a set bit means:

    ink = pixel_bit XOR (palette[0] is dark)

So swapping the two palette entries changes nothing on the panel -- the parser
just compensates. Only flipping the actual pixel data changes the picture.
This script therefore inverts the pixel bytes and leaves the palette alone.

USAGE
-----
    python flip_bmp.py                        # both files in the current folder
    python flip_bmp.py Sleep.bmp              # one file
    python flip_bmp.py *.bmp --out inverted   # write to a folder instead
    python flip_bmp.py Sleep.bmp --preview    # show before/after, change nothing

By default the original is kept as <name>.bak. Use --no-backup to skip that,
--out DIR to leave originals untouched entirely.

Pure standard library -- no Pillow, nothing to install.
"""

import argparse
import os
import struct
import sys

EXPECT_W, EXPECT_H = 296, 152          # the panel; anything else gets a warning
RAW_BYTES = (EXPECT_W // 8) * EXPECT_H  # 5624, for headerless raw dumps


class BmpError(Exception):
    pass


def parse_bmp(data):
    """Return (info dict) for an uncompressed 1-bpp BMP, or raise BmpError."""
    if len(data) < 30 or data[0:2] != b"BM":
        raise BmpError("not a BMP (no 'BM' signature)")
    data_off = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    width = struct.unpack_from("<i", data, 18)[0]
    height = struct.unpack_from("<i", data, 22)[0]
    bpp = struct.unpack_from("<H", data, 28)[0]
    comp = struct.unpack_from("<I", data, 30)[0]

    if bpp != 1:
        raise BmpError(
            f"{bpp}-bit image; the firmware needs 1-bit monochrome. "
            "Re-export as a 1-bit / monochrome BMP first."
        )
    if comp != 0:
        raise BmpError(f"compressed BMP (compression={comp}); needs uncompressed")

    top_down = height < 0
    abs_h = -height if top_down else height
    row_size = ((width * bpp + 31) // 32) * 4
    end = data_off + row_size * abs_h
    if end > len(data):
        raise BmpError(
            f"truncated: header says pixel data ends at {end}, file is {len(data)} bytes"
        )

    pal_off = 14 + dib_size
    pal0 = data[pal_off:pal_off + 4]
    # BMP palette entries are B,G,R,reserved
    pal0_dark = (sum(pal0[:3]) < 384) if len(pal0) >= 3 else True

    return {
        "data_off": data_off, "width": width, "height": abs_h,
        "top_down": top_down, "row_size": row_size, "end": end,
        "pal0_dark": pal0_dark,
    }


def ink_grid(data, info):
    """Rows of booleans, True = black ink, matching loadScreenBmp()'s logic."""
    grid = []
    for y in range(info["height"]):
        # positive height means rows are stored bottom-up
        src_y = y if info["top_down"] else (info["height"] - 1 - y)
        start = info["data_off"] + src_y * info["row_size"]
        row = data[start:start + info["row_size"]]
        line = []
        for x in range(info["width"]):
            bit = (row[x >> 3] >> (7 - (x & 7))) & 1
            line.append(bool(bit) != info["pal0_dark"])  # XOR
        grid.append(line)
    return grid


def preview(grid, cols=74, rows=19):
    """Coarse ASCII rendering so you can check the result without flashing."""
    h, w = len(grid), len(grid[0])
    out = []
    for r in range(rows):
        line = []
        for c in range(cols):
            y0, y1 = r * h // rows, max(r * h // rows + 1, (r + 1) * h // rows)
            x0, x1 = c * w // cols, max(c * w // cols + 1, (c + 1) * w // cols)
            cells = [grid[y][x] for y in range(y0, y1) for x in range(x0, x1)]
            frac = sum(cells) / len(cells)
            line.append(" .:-=+*#@"[min(8, int(frac * 9))])
        out.append("".join(line))
    return out


def side_by_side(before, after):
    print(f"  {'BEFORE':<{len(before[0])}}   {'AFTER'}")
    for a, b in zip(before, after):
        print(f"  {a}   {b}")


def flip_bytes(data, info):
    """Invert only the pixel-data region; header and palette are untouched."""
    out = bytearray(data)
    for i in range(info["data_off"], info["end"]):
        out[i] ^= 0xFF
    return bytes(out)


def process(path, args):
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        print(f"[SKIP] {path}: {e}")
        return False

    raw_mode = data[0:2] != b"BM"
    if raw_mode:
        if len(data) != RAW_BYTES:
            print(f"[SKIP] {path}: no BMP header and not a {RAW_BYTES}-byte raw dump "
                  f"({len(data)} bytes)")
            return False
        print(f"[INFO] {path}: headerless raw dump, inverting all {len(data)} bytes")
        info = {"data_off": 0, "width": EXPECT_W, "height": EXPECT_H,
                "top_down": True, "row_size": EXPECT_W // 8,
                "end": len(data), "pal0_dark": True}
    else:
        try:
            info = parse_bmp(data)
        except BmpError as e:
            print(f"[SKIP] {path}: {e}")
            return False
        print(f"[INFO] {path}: {info['width']}x{info['height']} 1-bit, "
              f"{'top-down' if info['top_down'] else 'bottom-up'}, "
              f"palette[0]={'black' if info['pal0_dark'] else 'white'}, "
              f"row stride {info['row_size']}")
        if (info["width"], info["height"]) != (EXPECT_W, EXPECT_H):
            print(f"[WARN] {path}: expected {EXPECT_W}x{EXPECT_H} for this panel; "
                  "the firmware will reject it and fall back to a plain screen")

    flipped = flip_bytes(data, info)

    if args.preview or args.verbose:
        side_by_side(preview(ink_grid(data, info)),
                     preview(ink_grid(flipped, info)))
    if args.preview:
        print(f"[DRY RUN] {path} not modified")
        return True

    if args.out:
        os.makedirs(args.out, exist_ok=True)
        dest = os.path.join(args.out, os.path.basename(path))
    else:
        dest = path
        if not args.no_backup:
            bak = path + ".bak"
            if os.path.exists(bak):
                print(f"[WARN] {bak} exists; leaving it alone (it is your original)")
            else:
                with open(bak, "wb") as f:
                    f.write(data)
                print(f"[OK]   backup -> {bak}")

    with open(dest, "wb") as f:
        f.write(flipped)
    print(f"[OK]   inverted -> {dest}")
    return True


def main():
    ap = argparse.ArgumentParser(
        description="Invert the pixels of 1-bit BMP sleep-screen backgrounds.")
    ap.add_argument("files", nargs="*", help="BMP files (default: Sleep.bmp DeepSleep.bmp)")
    ap.add_argument("--out", metavar="DIR", help="write to DIR instead of in place")
    ap.add_argument("--no-backup", action="store_true", help="don't keep a .bak copy")
    ap.add_argument("--preview", action="store_true",
                    help="show before/after as ASCII and change nothing")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="show before/after as ASCII and still write")
    args = ap.parse_args()

    files = args.files or ["Sleep.bmp", "DeepSleep.bmp"]
    missing = [f for f in files if not os.path.exists(f)]
    for f in missing:
        print(f"[SKIP] {f}: not found")
    todo = [f for f in files if os.path.exists(f)]
    if not todo:
        print("\nNothing to do. Run this from the folder holding the BMPs, "
              "or pass paths:\n    python flip_bmp.py path\\to\\Sleep.bmp")
        return 1

    ok = sum(process(f, args) for f in todo)
    print(f"\n{ok} of {len(todo)} file(s) processed.")
    if ok and not args.preview:
        print("Copy them to the SD card root and reboot; the serial log will "
              "show '[BG] ... loaded' for each.")
    return 0 if ok == len(todo) else 1


if __name__ == "__main__":
    sys.exit(main())
