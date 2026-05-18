import os
import subprocess
import re
import unicodedata
from yt_dlp import YoutubeDL

# Introduce data from keyboard
ALBUM_OR_PLAYLIST_URL = input("[>] Enter the link to the Youtube Music playlist: ").strip()
PLAYLIST_DISPLAY_NAME = input("[>] Enter the name of the playlist (for 000.txt): ").strip()

print("\n[!] Introduce the exact names for folders (ex: 01, 02, 03...):")
FOLDER_05  = input(" -> Folder Name for 0.5x: ").strip()
FOLDER_075 = input(" -> Folder Name for 0.75x: ").strip()
FOLDER_10  = input(" -> Folder Name for 1.0x (Original): ").strip()
FOLDER_125 = input(" -> Folder Name for 1.25x: ").strip()
FOLDER_15  = input(" -> Folder Name for 1.5x: ").strip()
METADATA_DIR = input(" -> Folder Name for metadata (ex: 01): ").strip()

# Create the directors
FOLDER_MAPPING = {
    "0.5":  FOLDER_05,
    "0.75": FOLDER_075,
    "1.0":  FOLDER_10,
    "1.25": FOLDER_125,
    "1.5":  FOLDER_15
}

for folder_name in FOLDER_MAPPING.values():
    os.makedirs(folder_name, exist_ok=True)
os.makedirs(METADATA_DIR, exist_ok=True)

# Function to convert all asiatic / full width strange characters (ex: ＡＢＣ１２３, ℌ𝔢𝔩𝔩𝔬, ①)
# in standards ones compatible with u8g2
def clean_str(text):
    new_text = unicodedata.normalize('NFKC', text)
    return new_text

# Get the duration of a song
def get_duration_str(file_path):
    cmd = [
        'ffprobe', '-v', 'error', '-show_entries', 'format=duration',
        '-of', 'default=noprint_wrappers=1:nokey=1', file_path
    ]
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    seconds = float(result.stdout.decode().strip())
    
    mins = int(seconds // 60)
    secs = round(seconds % 60)
    if secs == 60:
        mins += 1
        secs = 0
            
    return f"{mins:02d}:{secs:02d}"

# Save the playlist name "
PLAYLIST_DISPLAY_NAME = clean_str(PLAYLIST_DISPLAY_NAME)
txt_000_path = os.path.join(METADATA_DIR, "000.txt")

with open(txt_000_path, "w", encoding = "utf-8") as f_000:
    f_000.write(f"{PLAYLIST_DISPLAY_NAME}\n")

print(f"\n[+] Generated the file `./{METADATA_DIR}/000.txt` which contains: {PLAYLIST_DISPLAY_NAME}")

# Download the songs
print(f"[*] Downloading the songs from the base directory `./{FOLDER_MAPPING['1.0']}`...")

folder_1x = FOLDER_MAPPING["1.0"]
ydl_opts = {
    'format': 'bestaudio/best',
    'outtmpl': os.path.join(folder_1x, '%(playlist_index)s_-%_-%(title)s.%(ext)s'),
    'postprocessors': [{
        'key': 'FFmpegExtractAudio',
        'preferredcodec': 'mp3',
        'preferredquality': '192',
    }],
    'quiet': True
}

with YoutubeDL(ydl_opts) as ydl:
    ydl.download([ALBUM_OR_PLAYLIST_URL])

# Get metadata and rename downloaded songs
print("\n[*] Get metadata and rename downloaded songs...")

def get_song_index(file_name):
    match = re.match(r'^(\d+)_-%', file_name)
    return int(match.group(1)) if match else 999

raw_files = [f for f in os.listdir(folder_1x) if f.endswith('.mp3')]
raw_files.sort(key=get_song_index)

for track_index, file_name in enumerate(raw_files, start=1):
    mp3_name = f"{track_index:03d}.mp3"
    txt_name = f"{track_index:03d}.txt"
    
    old_mp3_path = os.path.join(folder_1x, file_name)
    new_mp3_path = os.path.join(folder_1x, mp3_name)
    
    clean_title = file_name.split("_-%_-")[-1].replace(".mp3", "")
    clean_title = clean_str(clean_title)
    duration_str = get_duration_str(old_mp3_path)

    os.rename(old_mp3_path, new_mp3_path)
    txt_path = os.path.join(METADATA_DIR, txt_name)
    with open(txt_path, "w", encoding="utf-8") as txt_file:
        txt_file.write(f"{clean_title}\n")
        txt_file.write(f"{duration_str}\n")
        
    print(f" -> [{mp3_name}] & [{txt_name}] Song processed: {clean_title} ({duration_str})")

# 3. Generate the audios for the others speed
print("\n[*] Generate the audio files in the next speed folders...")

songs_to_process = sorted([f for f in os.listdir(folder_1x) if f.endswith('.mp3')])
for speed_str, target_folder in FOLDER_MAPPING.items():
    if speed_str == "1.0":
        continue

    print(f" -> Generate speed {speed_str}x în the folder `./{target_folder}`...")
    
    for mp3_file in songs_to_process:
        input_path = os.path.join(folder_1x, mp3_file)
        output_path = os.path.join(target_folder, mp3_file)
        
        cmd = [
            'ffmpeg', '-y', '-i', input_path,
            '-filter:a', f'aresample=resample_cutoff=0.98,atempo={speed_str}',
            '-c:a', 'libmp3lame', '-b:a', '192k',
            '-vn', output_path
        ]
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

print("\n[+] Success!")
