import os
from PIL import Image

# =====================================================
# CONFIGURATION
# =====================================================
WIDTH = 296
HEIGHT = 152
# 296 * 152 / 8 bits per byte = 5624 bytes
EXPECTED_SIZE = (WIDTH * HEIGHT) // 8  

# Specify the paths to your two images here (PNG, JPG, BMP, etc.)
TARGET_IMAGES = [
    r"C:\Users\evant\Desktop\MP3 Project\Sleep.png",
    r"C:\Users\evant\Desktop\MP3 Project\DeepSleep.png",
]

# Set to True if your e-paper display requires inverted bits (black <-> white)
INVERT_BITS = True  

# Set to True to overwrite the original file with raw bytes (like your original script),
# or False to save as a separate '.raw' or '.bin' file.
OVERWRITE_ORIGINAL = False

# =====================================================
# CONVERT FUNCTION
# =====================================================
def convert_image_to_raw(image_path, invert=False, overwrite=False):
    try:
        if not os.path.exists(image_path):
            print(f"[!] File not found: {image_path}")
            return False

        # 1. Open and check dimensions
        img = Image.open(image_path)
        if img.size != (WIDTH, HEIGHT):
            print(f"[!] Resizing {image_path} from {img.size} to ({WIDTH}, {HEIGHT})")
            img = img.resize((WIDTH, HEIGHT))

        # 2. Convert to 1-bit monochrome
        img = img.convert("1")
        raw_data = img.tobytes()

        # Safety check
        if len(raw_data) != EXPECTED_SIZE:
            print(f"[!] Unexpected raw size ({len(raw_data)} bytes) for {image_path}")
            return False

        # 3. Optional bit inversion (255 - b)
        if invert:
            raw_data = bytearray(255 - b for b in raw_data)

        # 4. Determine output destination
        if overwrite:
            output_path = image_path
        else:
            output_path = os.path.splitext(image_path)[0] + ".raw"

        # 5. Save raw bytes
        with open(output_path, "wb") as f:
            f.write(raw_data)

        print(f"[+] Processed: {image_path} -> {output_path} ({EXPECTED_SIZE} bytes)")
        return True

    except Exception as e:
        print(f"[!] Failed to process {image_path}")
        print(f"    Error: {e}")
        return False

# =====================================================
# MAIN
# =====================================================
if __name__ == "__main__":
    print("=" * 60)
    print(f"Starting conversion for 296x152 images ({EXPECTED_SIZE} bytes)...")
    print("=" * 60)

    for path in TARGET_IMAGES:
        convert_image_to_raw(path, invert=INVERT_BITS, overwrite=OVERWRITE_ORIGINAL)

    print("=" * 60)
    print("Done!")