#!/usr/bin/env python3
"""
esp32_music_tool.py -- one tool for everything the ESP32 WAV player needs on its
SD card. Replaces convertToWAV.py, preprocessmusic.py, fixBMP.py and flip_bmp.py.

    python esp32_music_tool.py library      # m4a/mp3/... -> wav + art + tags + catalog
    python esp32_music_tool.py art          # re-invert existing 2888-byte art dumps
    python esp32_music_tool.py screens      # build/flip Sleep.bmp + DeepSleep.bmp
    python esp32_music_tool.py check E:\\    # validate an SD card against the firmware

Add --help to any subcommand for its options.

=======================================================================
WHAT CHANGED FROM THE FOUR ORIGINAL SCRIPTS -- read before running
=======================================================================

1. AUDIO IS MONO, AND THAT IS CORRECT.
   preprocessmusic.py used "-ac 1". The handoff spec says the firmware wants
   "44.1 kHz 16-bit stereo". The SPEC IS WRONG. get_audio_data() pops want*2
   bytes and copies ONE 16-bit sample to both channels, and BYTES_PER_SEC is
   44100*2 = 88200 = mono 16-bit. Feeding it a stereo file makes it read L,R,L,R
   as consecutive mono samples: double speed, wrong pitch. Kept at -ac 1.

2. ARTWORK WAS BEING INVERTED TWICE.
   preprocessmusic.py already flips polarity ("arr = 255 - arr") before writing,
   and fixBMP.py then flipped every .bmp on the card again -- which cancels out.
   Inversion is now ONE setting (--art-polarity) applied once, and the `art`
   subcommand exists only to repair cards processed by the old pair.

3. LONG PATHS ARE A WARNING, NOT A FAILURE. An earlier build of this tool
   refused to convert songs whose catalog path hit the firmware limit. That
   was wrong: the file is fine, and the real fix belonged in the firmware,
   which now allows 256 chars (v1.10.4, was 128) and LOGS the skip instead of
   dropping the line without a word.

4. FAILED CONVERSIONS WERE STILL ADDED TO catalog.txt.
   convert_audio() sent ffmpeg's stderr to DEVNULL and never checked the return
   code, so a song that failed to convert still got a catalog entry. The player
   then can't open it, and ten of those in a row stop playback
   (consecutiveOpenFails). Now the return code AND the output file are checked,
   and only real files are catalogued.

5. A SONG SITTING DIRECTLY IN THE INPUT FOLDER PRODUCED A BROKEN PATH.
   os.path.relpath returns "." for those, so the entry became "/./Song.wav".
   Nothing in the firmware normalises that away. Fixed.

6. THE DITHER WAS NOT ATKINSON.
   The neighbour list had (1,1) twice and no (-1,1), so error only ever moved
   right and down -- it smears diagonally instead of dispersing. The correct
   kernel is now the default; --dither legacy reproduces the old look exactly if
   you prefer what you already have on the card.

7. convertToWAV.py IS NOT MERGED. It targeted 16 kHz 8-bit mono for an Arduino
   Nano, which this firmware cannot play, and it also crashes on line 4
   (file_size_bytes is used before it exists). Nothing in it was worth keeping.

Also added: skip-already-done (with --force), path-length and catalog-size
checks against the firmware's real limits, and a `check` subcommand that reads a
finished card the way the player will.
"""

import argparse
import os
import struct
import subprocess
import sys

# ---------------------------------------------------------------- firmware facts
SRC_EXTS      = (".m4a", ".mp3", ".flac", ".wav", ".ogg", ".opus", ".aac", ".wma")
SAMPLE_RATE   = 44100
CHANNELS      = 1            # see note 1 above -- do not change to 2
BYTES_PER_SEC = SAMPLE_RATE * 2
ART_SIZE      = (152, 152)
ART_BYTES     = (ART_SIZE[0] * ART_SIZE[1]) // 8        # 2888
SCREEN_SIZE   = (296, 152)
SCREEN_ROW    = SCREEN_SIZE[0] // 8                      # 37
SCREEN_BYTES  = SCREEN_ROW * SCREEN_SIZE[1]              # 5624
# 2026-08-10: v1.10.4 raised the firmware limit from 128 to 256.
# Pass --max-path 128 if you are running v1.10.3 or older.
MAX_PATH_LEN  = 256          # firmware skips catalog lines >= this
MAX_CATALOG   = 2000         # firmware stops reading past this many
WAV_HEADER    = 44           # firmware assumes data starts at byte 44

try:
    import numpy as np
    from PIL import Image
    HAVE_IMG = True
except ImportError:
    HAVE_IMG = False

try:
    from mutagen import File as MutagenFile
    HAVE_TAGS = True
except ImportError:
    HAVE_TAGS = False


def die(msg):
    print(f"\n[FATAL] {msg}")
    sys.exit(1)


# ================================================================== dithering
def dither_atkinson(image, size=ART_SIZE, legacy=False, strength_scale=True):
    """Centre-crop to square, resize, and error-diffuse to 1-bit."""
    image = image.convert("L")
    w, h = image.size
    m = min(w, h)
    image = image.crop(((w - m) / 2, (h - m) / 2, (w - m) / 2 + m, (h - m) / 2 + m))
    image = image.resize(size, Image.Resampling.LANCZOS)

    arr = np.array(image, dtype=float)
    avg = float(np.mean(arr))
    strength = (avg / 255.0) if strength_scale else 1.0
    threshold = min(max(avg, 64), 192)

    if legacy:
        # Exactly the original list, (1,1) duplicated and (-1,1) missing.
        neighbours = [(1, 0), (2, 0), (1, 1), (0, 1), (1, 1), (0, 2)]
    else:
        neighbours = [(1, 0), (2, 0), (-1, 1), (0, 1), (1, 1), (0, 2)]

    height, width = arr.shape
    for y in range(height):
        for x in range(width):
            old = arr[y, x]
            new = 255.0 if old > threshold else 0.0
            arr[y, x] = new
            err = (old - new) * strength * 0.125
            for dx, dy in neighbours:
                nx, ny = x + dx, y + dy
                if 0 <= nx < width and 0 <= ny < height:
                    arr[ny, nx] += err
    return arr


def arr_to_ink_bits(arr, invert):
    """
    arr: 0=black, 255=white. Returns a PIL '1' image whose SET BITS mean INK.

    drawBitmap() paints 1 bits black, so a set bit must mean "black here".
    PIL mode '1' sets a bit where the pixel is WHITE, so the array is inverted
    before conversion -- this is the single polarity step (see note 2).
    `invert=False` gives the opposite convention, for a card whose artwork was
    made by the old double-inverting pipeline.
    """
    out = (255.0 - arr) if invert else arr
    return Image.fromarray(out.astype("uint8"), mode="L").convert("1")


def save_raw_art(img, path):
    """Raw framebuffer for loadArtwork(), which reads bytes with NO header."""
    img = img.convert("1")
    if img.size != ART_SIZE:
        raise ValueError(f"artwork must be {ART_SIZE}, got {img.size}")
    raw = img.tobytes()          # 152/8 = 19 bytes/row exactly, no PIL padding
    if len(raw) != ART_BYTES:
        raise ValueError(f"expected {ART_BYTES} bytes, got {len(raw)}")
    with open(path, "wb") as f:
        f.write(raw)


# ============================================================ real BMP writer
def write_1bit_bmp(bits_rows, path):
    """
    Write a genuine 1-bpp BMP for the SLEEP SCREENS. Unlike the artwork dumps,
    loadScreenBmp() parses the real format, so this must get right:
      - rows padded to a 4-byte boundary (296 px = 37 bytes -> 40 stride)
      - bottom-up row order (positive height)
      - palette[0] = black, so a SET BIT means WHITE; the firmware reads the
        palette and normalises, which is why polarity is handled here and not
        by flipping the palette (that would be a no-op).
    bits_rows: list of rows of bools, True = INK (black), top row first.
    """
    w, h = SCREEN_SIZE
    stride = ((w + 31) // 32) * 4
    rows = []
    for y in range(h - 1, -1, -1):          # bottom-up
        b = bytearray(stride)
        for x in range(w):
            if not bits_rows[y][x]:         # ink=False -> white -> bit 1
                b[x >> 3] |= 0x80 >> (x & 7)
        rows.append(bytes(b))
    palette = b"\x00\x00\x00\x00" + b"\xff\xff\xff\x00"   # [0]=black [1]=white
    dib = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 1, 0, stride * h, 2835, 2835, 2, 0)
    off = 14 + 40 + 8
    fh = b"BM" + struct.pack("<IHHI", off + stride * h, 0, 0, off)
    with open(path, "wb") as f:
        f.write(fh + dib + palette + b"".join(rows))


def read_1bit_bmp(path):
    """Inverse of the above; returns rows of bools (True = ink), or None."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"BM":
        if len(data) != SCREEN_BYTES:
            return None
        rows = []
        for y in range(SCREEN_SIZE[1]):
            r = data[y * SCREEN_ROW:(y + 1) * SCREEN_ROW]
            rows.append([bool((r[x >> 3] >> (7 - (x & 7))) & 1) for x in range(SCREEN_SIZE[0])])
        return rows
    off = struct.unpack_from("<I", data, 10)[0]
    dib = struct.unpack_from("<I", data, 14)[0]
    w = struct.unpack_from("<i", data, 18)[0]
    hh = struct.unpack_from("<i", data, 22)[0]
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp != 1:
        return None
    top_down, h = hh < 0, abs(hh)
    if (w, h) != SCREEN_SIZE:
        return None
    stride = ((w + 31) // 32) * 4
    p0 = data[14 + dib:14 + dib + 3]
    pal0_dark = sum(p0) < 384 if len(p0) == 3 else True
    grid = [None] * h
    for i in range(h):
        y = i if top_down else (h - 1 - i)
        r = data[off + i * stride: off + i * stride + stride]
        grid[y] = [(bool((r[x >> 3] >> (7 - (x & 7))) & 1) != pal0_dark) for x in range(w)]
    return grid


def ascii_preview(grid, cols=74, rows=19):
    h, w = len(grid), len(grid[0])
    out = []
    for r in range(rows):
        line = []
        for c in range(cols):
            y0, y1 = r * h // rows, max(r * h // rows + 1, (r + 1) * h // rows)
            x0, x1 = c * w // cols, max(c * w // cols + 1, (c + 1) * w // cols)
            cells = [grid[y][x] for y in range(y0, y1) for x in range(x0, x1)]
            line.append(" .:-=+*#@"[min(8, int(sum(cells) / len(cells) * 9))])
        out.append("".join(line))
    return out


# ==================================================================== metadata
def read_tags(path):
    """(title, artist, album, 'M:SS'). Falls back to the path when tags are
    missing or mutagen isn't installed -- the same shape loadMetadata() expects
    in the .txt sidecar: four lines, in that order."""
    title = artist = album = ""
    seconds = 0
    if HAVE_TAGS:
        try:
            m = MutagenFile(path)
            if m is not None:
                if getattr(m, "info", None):
                    seconds = int(getattr(m.info, "length", 0) or 0)
                t = m.tags or {}

                def first(*keys):
                    for k in keys:
                        if k in t:
                            v = t[k]
                            if isinstance(v, list) and v:
                                return str(v[0])
                            if v:
                                return str(v)
                    return ""
                title = first("\xa9nam", "TIT2", "title")
                artist = first("\xa9ART", "TPE1", "artist")
                album = first("\xa9alb", "TALB", "album")
        except Exception:
            pass
    base = os.path.splitext(os.path.basename(path))[0]
    parent = os.path.basename(os.path.dirname(path))
    grand = os.path.basename(os.path.dirname(os.path.dirname(path)))
    return (title or base, artist or grand or "Unknown Artist",
            album or parent or "", f"{seconds // 60}:{seconds % 60:02d}")


def write_meta_txt(path, title, artist, album, length):
    with open(path, "w", encoding="utf-8") as f:
        f.write(f"{title}\n{artist}\n{album}\n{length}\n")


# ==================================================================== audio
def convert_audio(src, dst):
    """44.1 kHz 16-bit MONO PCM WAV. Returns (ok, message)."""
    cmd = ["ffmpeg", "-y", "-loglevel", "error", "-i", src,
           "-ar", str(SAMPLE_RATE), "-ac", str(CHANNELS),
           "-c:a", "pcm_s16le", "-f", "wav", dst]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True)
    except FileNotFoundError:
        return False, "ffmpeg not found on PATH"
    if r.returncode != 0:
        return False, (r.stderr or "").strip().splitlines()[-1] if r.stderr else "ffmpeg failed"
    if not os.path.exists(dst):
        return False, "ffmpeg reported success but wrote no file"
    if os.path.getsize(dst) <= WAV_HEADER:
        return False, "output is header-only (no audio)"
    return True, ""


# ================================================================== subcommands
def cmd_library(a):
    if not HAVE_IMG:
        die("numpy and Pillow are required for artwork.  pip install numpy pillow")
    if not os.path.isdir(a.input):
        die(f"input folder not found: {a.input}")
    os.makedirs(a.output, exist_ok=True)

    sources = []
    for root, _, files in os.walk(a.input):
        for fn in files:
            if fn.lower().endswith(SRC_EXTS):
                sources.append(os.path.join(root, fn))
    sources.sort()
    if not sources:
        die(f"no audio files under {a.input}")
    print(f"Found {len(sources)} source file(s)\n")

    entries, failed, skipped, warnings = [], [], 0, []

    for i, src in enumerate(sources, 1):
        base = os.path.splitext(os.path.basename(src))[0]
        rel = os.path.relpath(os.path.dirname(src), a.input)
        # relpath gives "." for files sitting directly in the input folder;
        # joining that produced "/./Song.wav" in the original script.
        rel = "" if rel == "." else rel.replace("\\", "/")
        outdir = os.path.join(a.output, rel) if rel else a.output
        os.makedirs(outdir, exist_ok=True)

        wav = os.path.join(outdir, base + ".wav")
        art = os.path.join(outdir, base + ".bmp")
        txt = os.path.join(outdir, base + ".txt")
        entry = "/" + (rel + "/" if rel else "") + base + ".wav"

        # 2026-08-10: this used to refuse to convert the song and count it as
        # a failure. Wrong on both counts -- the .wav is fine, and refusing to
        # write it is a bigger action than the firmware takes. Convert it,
        # catalogue it, warn. The real fix was in the firmware (v1.10.4:
        # limit 128 -> 256, and the skip is logged instead of silent).
        if len(entry) >= a.max_path:
            warnings.append("path is %d chars, firmware limit is %d -- this song "
                            "will NOT appear in the player: %s"
                            % (len(entry), a.max_path - 1, entry))
        if any(ord(c) > 126 for c in entry):
            warnings.append(f"non-ASCII in path, may render as garbage: {entry}")

        if not a.force and os.path.exists(wav) and os.path.getsize(wav) > WAV_HEADER:
            skipped += 1
            entries.append(entry)
            if a.verbose:
                print(f"[{i}/{len(sources)}] skip (exists): {base}")
            continue

        print(f"[{i}/{len(sources)}] {base}")
        title, artist, album, length = read_tags(src)
        write_meta_txt(txt, title, artist, album, length)

        if not a.no_art:
            cover = extract_cover(src)
            if cover is not None:
                try:
                    arr = dither_atkinson(cover, legacy=(a.dither == "legacy"),
                                          strength_scale=not a.full_strength)
                    save_raw_art(arr_to_ink_bits(arr, invert=a.art_polarity == "normal"), art)
                except Exception as e:
                    print(f"    [!] artwork failed: {e}")
            elif a.verbose:
                print("    (no embedded cover)")

        ok, msg = convert_audio(src, wav)
        if not ok:
            print(f"    [!] audio FAILED: {msg}")
            failed.append((src, msg))
            for p in (wav, art, txt):        # don't leave half a song behind
                if os.path.exists(p):
                    try:
                        os.remove(p)
                    except OSError:
                        pass
            continue
        entries.append(entry)

    entries.sort()
    if len(entries) > MAX_CATALOG:
        warnings.append(f"{len(entries)} songs exceeds MAX_CATALOG={MAX_CATALOG}; "
                        f"the firmware will ignore the last {len(entries)-MAX_CATALOG}")
    cat = os.path.join(a.output, "catalog.txt")
    with open(cat, "w", encoding="utf-8", newline="\n") as f:
        for e in entries:
            f.write(e + "\n")

    print("\n" + "=" * 62)
    print(f"catalogued : {len(entries)}   (skipped {skipped} already done)")
    print(f"failed     : {len(failed)}")
    print(f"catalog    : {cat}")
    for w in warnings[:12]:
        print(f"  [warn] {w}")
    if len(warnings) > 12:
        print(f"  ... and {len(warnings)-12} more warnings")
    for s, m in failed[:10]:
        print(f"  [fail] {os.path.basename(s)}: {m}")
    print("=" * 62)
    return 1 if failed else 0


def extract_cover(path):
    """Embedded cover art as a PIL image, or None."""
    if not HAVE_TAGS:
        return None
    import io
    try:
        m = MutagenFile(path)
        if m is None or not m.tags:
            return None
        t = m.tags
        if "covr" in t and t["covr"]:
            return Image.open(io.BytesIO(bytes(t["covr"][0])))
        for k in t.keys():
            if str(k).startswith("APIC"):
                return Image.open(io.BytesIO(t[k].data))
        pics = getattr(m, "pictures", None)
        if pics:
            return Image.open(io.BytesIO(pics[0].data))
    except Exception:
        return None
    return None


def cmd_art(a):
    """Repair artwork polarity on a card built by the old two-script pipeline."""
    n = ok = 0
    for root, _, files in os.walk(a.path):
        for fn in files:
            if not fn.lower().endswith(".bmp"):
                continue
            p = os.path.join(root, fn)
            size = os.path.getsize(p)
            if size != ART_BYTES:
                if a.verbose:
                    print(f"[skip] {p} ({size} bytes, not a {ART_BYTES}-byte art dump)")
                continue
            n += 1
            if a.dry_run:
                print(f"[would invert] {p}")
                ok += 1
                continue
            with open(p, "rb") as f:
                d = f.read()
            with open(p, "wb") as f:
                f.write(bytes(b ^ 0xFF for b in d))
            ok += 1
            if a.verbose:
                print(f"[inverted] {p}")
    print(f"\n{ok} of {n} artwork file(s) {'would be ' if a.dry_run else ''}inverted.")
    print("Inverting twice restores the original, so a mistake here costs nothing.")
    return 0


def cmd_screens(a):
    if a.source:
        if not HAVE_IMG:
            die("Pillow/numpy needed to build a screen from an image")
        img = Image.open(a.source).convert("L").resize(SCREEN_SIZE, Image.Resampling.LANCZOS)
        arr = np.array(img, dtype=float)
        thr = float(np.mean(arr))
        grid = [[arr[y][x] <= thr for x in range(SCREEN_SIZE[0])] for y in range(SCREEN_SIZE[1])]
        if a.invert:
            grid = [[not v for v in row] for row in grid]
        write_1bit_bmp(grid, a.dest)
        print(f"[OK] wrote {a.dest} ({SCREEN_SIZE[0]}x{SCREEN_SIZE[1]}, 1-bit)")
        if a.preview:
            for line in ascii_preview(grid):
                print("  " + line)
        return 0

    targets = a.files or ["Sleep.bmp", "DeepSleep.bmp"]
    done = 0
    for p in targets:
        if not os.path.exists(p):
            print(f"[SKIP] {p}: not found")
            continue
        grid = read_1bit_bmp(p)
        if grid is None:
            print(f"[SKIP] {p}: not a 1-bit {SCREEN_SIZE[0]}x{SCREEN_SIZE[1]} BMP or raw dump")
            continue
        flipped = [[not v for v in row] for row in grid]
        if a.preview:
            before, after = ascii_preview(grid), ascii_preview(flipped)
            print(f"\n{p}\n  {'BEFORE':<74}   AFTER")
            for b, c in zip(before, after):
                print(f"  {b}   {c}")
            print("  [dry run] not modified")
            done += 1
            continue
        if not a.no_backup and not os.path.exists(p + ".bak"):
            with open(p, "rb") as f, open(p + ".bak", "wb") as g:
                g.write(f.read())
            print(f"[OK]   backup -> {p}.bak")
        write_1bit_bmp(flipped, p)
        print(f"[OK]   inverted -> {p}")
        done += 1
    print(f"\n{done} screen file(s) processed.")
    return 0


def cmd_check(a):
    """Read a finished card the way the firmware will, and report what it'd see."""
    root = a.path
    problems, notes = [], []
    cat = os.path.join(root, "catalog.txt")
    if not os.path.exists(cat):
        problems.append("catalog.txt MISSING -- the player will boot to an empty playlist")
        entries = []
    else:
        with open(cat, encoding="utf-8", errors="replace") as f:
            entries = [l.strip() for l in f if l.strip()]
        print(f"catalog.txt: {len(entries)} entries")
        if len(entries) > MAX_CATALOG:
            problems.append(f"{len(entries)} entries; firmware reads only the first {MAX_CATALOG}")

    missing = long = noart = notxt = 0
    for e in entries[:MAX_CATALOG]:
        if len(e) >= a.max_path:
            long += 1
            if long <= 5:
                notes.append("%d chars, invisible to the player: %s" % (len(e), e))
            continue
        local = os.path.join(root, e.lstrip("/").replace("/", os.sep))
        if not os.path.exists(local):
            missing += 1
            if missing <= 5:
                notes.append(f"missing file: {e}")
            continue
        if os.path.getsize(local) <= WAV_HEADER:
            problems.append(f"header-only wav: {e}")
        stem = os.path.splitext(local)[0]
        if not os.path.exists(stem + ".bmp"):
            noart += 1
        elif os.path.getsize(stem + ".bmp") != ART_BYTES:
            problems.append(f"artwork is {os.path.getsize(stem+'.bmp')} bytes, "
                            f"needs {ART_BYTES}: {e}")
        if not os.path.exists(stem + ".txt"):
            notxt += 1
    if missing:
        problems.append(f"{missing} catalogued file(s) not on the card")
    if long:
        problems.append("%d path(s) >= %d chars -- the firmware drops these lines at "
                        "boot, so those songs never appear in the player" % (long, a.max_path))
    if noart:
        notes.append(f"{noart} song(s) with no artwork (screen shows a blank panel)")
    if notxt:
        notes.append(f"{notxt} song(s) with no .txt (title/artist taken from the path)")

    for name in ("Sleep.bmp", "DeepSleep.bmp"):
        p = os.path.join(root, name)
        if not os.path.exists(p):
            notes.append(f"{name} absent -- sleep screen falls back to plain black")
        elif read_1bit_bmp(p) is None:
            problems.append(f"{name} is not a 1-bit {SCREEN_SIZE[0]}x{SCREEN_SIZE[1]} BMP "
                            f"or {SCREEN_BYTES}-byte raw dump; firmware will reject it")
        else:
            print(f"{name}: OK")

    fw = os.path.join(root, "firmware.bin")
    if os.path.exists(fw):
        with open(fw, "rb") as f:
            magic = f.read(1)
        sz = os.path.getsize(fw)
        if magic != b"\xe9":
            problems.append(f"firmware.bin first byte is {magic.hex()}, expected e9 "
                            "-- the player will refuse it")
        elif sz < 64 * 1024:
            problems.append(f"firmware.bin is only {sz} bytes -- looks truncated")
        else:
            print(f"firmware.bin: {sz:,} bytes, magic OK -- will be offered at boot")

    print()
    for n in notes:
        print(f"  [note] {n}")
    for p in problems:
        print(f"  [PROBLEM] {p}")
    print("\n" + ("All checks passed." if not problems else f"{len(problems)} problem(s) found."))
    return 1 if problems else 0


# ======================================================================== cli
def main():
    ap = argparse.ArgumentParser(
        description="Prepare music, artwork and sleep screens for the ESP32 WAV player.",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("library", help="convert a music folder into an SD-ready tree")
    p.add_argument("--input", default=r"C:\Users\evant\Desktop\MP3 Project\ytmusic_downloader\YouTube Music")
    p.add_argument("--output", default=r"E:\\")
    p.add_argument("--force", action="store_true", help="redo songs that already have a .wav")
    p.add_argument("--no-art", action="store_true", help="skip artwork entirely (much faster)")
    p.add_argument("--dither", choices=["atkinson", "legacy"], default="atkinson",
                   help="'legacy' reproduces the original script's broken kernel")
    p.add_argument("--full-strength", action="store_true",
                   help="don't scale error diffusion by average luminance")
    p.add_argument("--art-polarity", choices=["normal", "inverted"], default="normal",
                   help="'normal' = set bit means black ink, which is what drawBitmap wants")
    p.add_argument("--max-path", type=int, default=MAX_PATH_LEN,
                   help="firmware MAX_PATH_LEN (256 on v1.10.4+, 128 before)")
    p.add_argument("-v", "--verbose", action="store_true")
    p.set_defaults(func=cmd_library)

    p = sub.add_parser("art", help="invert existing 2888-byte artwork dumps (fixBMP replacement)")
    p.add_argument("path", nargs="?", default="E:\\")
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("-v", "--verbose", action="store_true")
    p.set_defaults(func=cmd_art)

    p = sub.add_parser("screens", help="build or invert Sleep.bmp / DeepSleep.bmp")
    p.add_argument("files", nargs="*", help="default: Sleep.bmp DeepSleep.bmp")
    p.add_argument("--source", help="build a screen FROM this image instead of flipping")
    p.add_argument("--dest", default="Sleep.bmp", help="output when --source is used")
    p.add_argument("--invert", action="store_true", help="invert while building from --source")
    p.add_argument("--preview", action="store_true", help="ASCII before/after, change nothing")
    p.add_argument("--no-backup", action="store_true")
    p.set_defaults(func=cmd_screens)

    p = sub.add_parser("check", help="validate a finished SD card")
    p.add_argument("path", nargs="?", default="E:\\")
    p.add_argument("--max-path", type=int, default=MAX_PATH_LEN,
                   help="firmware MAX_PATH_LEN (256 on v1.10.4+, 128 before)")
    p.set_defaults(func=cmd_check)

    a = ap.parse_args()
    return a.func(a)


if __name__ == "__main__":
    sys.exit(main())