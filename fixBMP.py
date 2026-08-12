import os

# =====================================================
# CONFIG
# =====================================================
TARGET_DIR = r"E:"
EXPECTED_SIZE = 2888  # 152 * 152 / 8 bytes

# =====================================================
# RAW BIT INVERSION PROCESSOR
# =====================================================
def invert_raw_bitmap(file_path):
    try:
        # 1. Read the raw bytes
        with open(file_path, "rb") as f:
            raw_data = f.read()

        # Safety check: make sure it's actually one of your e-paper framebuffers
        if len(raw_data) != EXPECTED_SIZE:
            print(f"[!] Skipped (Unexpected size {len(raw_data)} bytes): {file_path}")
            return False

        # 2. Invert the bits using a bytearray
        # 255 - byte (or ~byte & 0xFF) flips 1s to 0s and 0s to 1s
        inverted_data = bytearray(255 - b for b in raw_data)

        # 3. Write the raw bytes back
        with open(file_path, "wb") as f:
            f.write(inverted_data)

        print(f"[+] Inverted raw bitmap: {file_path}")
        return True

    except Exception as e:
        print(f"[!] Failed to process {file_path}")
        print(f"    Error: {e}")
        return False

def process_directory(directory):
    if not os.path.exists(directory):
        print(f"[!] Directory does not exist: {directory}")
        return

    print("=" * 60)
    print(f"Starting RAW 1-bit bitmap inversion in: {directory}")
    print("=" * 60)

    success_count = 0
    total_files = 0

    for root, dirs, files in os.walk(directory):
        for file in files:
            # Even though they are raw headers, they still have the .bmp extension from your script
            if file.lower().endswith(".bmp"):
                total_files += 1
                file_path = os.path.join(root, file)
                
                if invert_raw_bitmap(file_path):
                    success_count += 1

    print("\n" + "=" * 60)
    print(f"Task Completed!")
    print(f"Successfully inverted {success_count} out of {total_files} raw files.")
    print("=" * 60)

# =====================================================
# MAIN
# =====================================================
if __name__ == "__main__":
    process_directory(TARGET_DIR)