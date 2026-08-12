import os
import subprocess
import io
import random
import numpy as np
from PIL import Image
from mutagen.mp4 import MP4

# =====================================================
# CONFIG
# =====================================================

INPUT_DIR = r"C:\Users\evant\Desktop\MP3 Project\ytmusic_downloader\YouTube Music"
OUTPUT_DIR = r"C:\Users\evant\Desktop\MP3 Project\ytmusic_downloader\ESP32_Music"

IMAGE_SIZE = (152, 152)

# =====================================================
# IMAGE PROCESSING
# =====================================================

def apply_web_style_atkinson(image, size=IMAGE_SIZE):
    image = image.convert("L")

    w, h = image.size
    min_dim = min(w, h)

    left = (w - min_dim) / 2
    top = (h - min_dim) / 2

    image = image.crop(
        (
            left,
            top,
            left + min_dim,
            top + min_dim
        )
    )

    image = image.resize(
        size,
        Image.Resampling.LANCZOS
    )

    arr = np.array(image, dtype=float)

    avg_lum = np.mean(arr)
    strength = avg_lum / 255.0
    threshold = min(max(avg_lum, 64), 192)

    height, width = arr.shape

    for y in range(height):
        for x in range(width):

            old_val = arr[y, x]

            new_val = 255 if old_val > threshold else 0

            arr[y, x] = new_val

            error = (old_val - new_val) * strength

            neighbors = [
                (1, 0),
                (2, 0),
                (1, 1),
                (0, 1),
                (1, 1),
                (0, 2)
            ]

            for dx, dy in neighbors:

                nx = x + dx
                ny = y + dy

                if (
                    0 <= nx < width and
                    0 <= ny < height
                ):
                    arr[ny, nx] += error * 0.125

    # -------------------------------------------------
    # FIX: Invert colors for the e-paper display
    # -------------------------------------------------
    arr = 255 - arr

    return Image.fromarray(
        arr.astype("uint8"),
        mode="L"
    ).convert("1")

# =====================================================
# RAW 1-BIT EXPORT
# =====================================================

def save_epaper_bitmap(img, output_path):
    """
    Creates a raw framebuffer file that matches:

        display.drawBitmap()

    Expected size:
        152 x 152 / 8 = 2888 bytes
    """

    img = img.convert("1")

    if img.size != IMAGE_SIZE:
        raise ValueError(
            f"Image size must be {IMAGE_SIZE}"
        )

    raw = img.tobytes()

    expected = (
        IMAGE_SIZE[0] *
        IMAGE_SIZE[1]
    ) // 8

    if len(raw) != expected:
        raise ValueError(
            f"Expected {expected} bytes, got {len(raw)}"
        )

    with open(output_path, "wb") as f:
        f.write(raw)

# =====================================================
# METADATA
# =====================================================

def write_metadata(audio, output_txt):

    title = audio.tags.get(
        '\xa9nam',
        ['Unknown Title']
    )[0]

    artist = audio.tags.get(
        '\xa9ART',
        ['Unknown Artist']
    )[0]

    album = audio.tags.get(
        '\xa9alb',
        ['Unknown Album']
    )[0]

    duration_sec = int(audio.info.length)

    mins = duration_sec // 60
    secs = duration_sec % 60

    with open(
        output_txt,
        "w",
        encoding="utf-8"
    ) as f:

        f.write(f"{title}\n")
        f.write(f"{artist}\n")
        f.write(f"{album}\n")
        f.write(f"{mins}:{secs:02d}\n")

# =====================================================
# WAV CONVERSION
# =====================================================

def convert_audio(
    input_file,
    output_wav
):

    if os.path.exists(output_wav):
        os.remove(output_wav)

    cmd = [
        "ffmpeg",
        "-y",
        "-i",
        input_file,

        "-ar",
        "44100",

        "-ac",
        "1",

        "-c:a",
        "pcm_s16le",

        output_wav
    ]

    subprocess.run(
        cmd,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )

# =====================================================
# SONG PROCESSING
# =====================================================

def process_song(
    m4a_path,
    output_dir,
    base_name
):

    try:

        audio = MP4(m4a_path)

        txt_path = os.path.join(
            output_dir,
            f"{base_name}.txt"
        )

        write_metadata(
            audio,
            txt_path
        )

        bmp_path = os.path.join(
            output_dir,
            f"{base_name}.bmp"
        )

        covers = audio.tags.get("covr")

        if covers:

            try:

                cover = Image.open(
                    io.BytesIO(covers[0])
                )

                cover = apply_web_style_atkinson(
                    cover
                )

                save_epaper_bitmap(
                    cover,
                    bmp_path
                )

            except Exception as e:

                print(
                    f"[!] Artwork failed: "
                    f"{base_name}"
                )

                print(e)

        wav_path = os.path.join(
            output_dir,
            f"{base_name}.wav"
        )

        convert_audio(
            m4a_path,
            wav_path
        )

        print(
            f"[+] Processed: "
            f"{base_name}"
        )

        return True

    except Exception as e:

        print(
            f"[!] Failed: "
            f"{base_name}"
        )

        print(e)

        return False

# =====================================================
# LIBRARY PROCESSOR
# =====================================================

def process_library():

    os.makedirs(
        OUTPUT_DIR,
        exist_ok=True
    )

    catalog_entries = []

    total = 0

    for root, dirs, files in os.walk(INPUT_DIR):

        for file in files:

            if not file.lower().endswith(".m4a"):
                continue

            total += 1

            m4a_path = os.path.join(
                root,
                file
            )

            base_name = os.path.splitext(
                file
            )[0]

            rel_path = os.path.relpath(
                root,
                INPUT_DIR
            )

            output_song_dir = os.path.join(
                OUTPUT_DIR,
                rel_path
            )

            os.makedirs(
                output_song_dir,
                exist_ok=True
            )

            success = process_song(
                m4a_path,
                output_song_dir,
                base_name
            )

            if success:

                catalog_entry = (
                    "/"
                    + rel_path.replace("\\", "/")
                    + "/"
                    + base_name
                    + ".wav"
                )

                catalog_entries.append(
                    catalog_entry
                )

    catalog_entries.sort()

    catalog_file = os.path.join(
        OUTPUT_DIR,
        "catalog.txt"
    )

    with open(
        catalog_file,
        "w",
        encoding="utf-8"
    ) as f:

        for entry in catalog_entries:
            f.write(entry + "\n")

    print()
    print("=" * 50)
    print(
        f"Processed {len(catalog_entries)} songs"
    )
    print(
        f"Catalog written to:"
    )
    print(catalog_file)
    print("=" * 50)

# =====================================================
# MAIN
# =====================================================

if __name__ == "__main__":

    process_library()