from pydub import AudioSegment
import os

print(f"  [+] New file size: {file_size_bytes / (1024*1024):.2f} MB")

def convert_to_arduino_wav(input_file, output_name="test.wav"):
    print(f"Processing {input_file}...")
    
    # 1. Load the m4a file
    audio = AudioSegment.from_file(input_file, format="m4a")
    
    # 2. Convert to Mono
    audio = audio.set_channels(1)
    
    # 3. Set Sample Rate to 16000Hz (Sweet spot for Nano)
    audio = audio.set_frame_rate(16000)
    
    # 4. Export as 8-bit PCM WAV
    # 'sample_width=1' corresponds to 8-bit
    audio.export(output_name, format="wav", parameters=["-acodec", "pcm_u8"])
    
    print(f"Success! Saved as {output_name}")
    print(f"File size: {os.path.getsize(output_name) / 1024:.2f} KB")

# Usage
convert_to_arduino_wav(r"C:\Users\evant\Desktop\08 Fly Me To The Moon (2008 Remastered) (feat. Count Bas.m4a", "test.wav")