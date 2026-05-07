import os
import shutil
import sys
import subprocess

# --- DEPENDENCY VALIDATION ---

def check_pydub():
	global AudioSegment
	try:
		from pydub import AudioSegment
	except ImportError:
		print("\n❌")
		print("DEPENDENCY ERROR: 'pydub' library not found.")
		print("Please run: pip install pydub.")
		sys.exit(-1)

def check_ffmpeg():
	try:
		subprocess.run(["ffmpeg", "-version"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
	except FileNotFoundError:
		print("\n❌")
		print("DEPENDENCY ERROR: FFmpeg engine not found.")
		print("Install FFmpeg (https://ffmpeg.org/) and add to System PATH.")
		sys.exit(-1)

def check_dependencies():
	check_ffmpeg()
	check_pydub()

# --- SYSTEM SETUP ---

def initialize_sd():
	sd_path = input("\nEnter SD Card Path (e.g. E:/ or /media/user/SD): ").strip().strip('"')
	
	# Valid path?
	if not os.path.exists(sd_path):
		print(f"\n❌ ERROR: Path '{sd_path}' is invalid.")
		print("Closing application...")
		sys.exit(-1)

	# First time? Clean the card then.
	print("\n❗ Is the first time when you connect this SD card at this app?")
	print("If yes, the card needs to be wiped.")
	wipe = input("Wipe SD card? (Press y for yes): ").strip().upper()

	# Clean the card
	if wipe == "Y":
		print(f"\n🧹 Cleaning SD Card...")
	
		try:
			for item in os.listdir(sd_path):

				item_path = os.path.join(sd_path, item)
				if os.path.isfile(item_path):
					os.unlink(item_path)
				elif os.path.isdir(item_path):
					shutil.rmtree(item_path)

			print("✅ SD Card is now clean.")
		except Exception as e:
			print(f"🚨 Warning during wipe: {e}")

	return sd_path

# --- CORE LOGIC ---

class TrackManager:
	def __init__(self, sd_card_path):
		self.sd_path = sd_card_path
		self.default_playlist_name = "Songs".upper()
		self.speeds = [0.5, 0.75, 1.0, 1.25, 1.5]

		# Create the default playlist if does not exist
		default_playlist_path = os.path.join(self.sd_path, self.default_playlist_name)
		if not os.path.exists(default_playlist_path):
			os.makedirs(default_playlist_path)


	def _is_song_in_playlist(self, song_name, playlist_path):
		if not os.path.exists(playlist_path):
			return False

		for item in os.listdir(playlist_path):
			if item.upper() == song_name.strip().upper():
				return True
		return False

	def _get_playlist_path(self, playlist_name):
		playlist_name = playlist_name
		if playlist_name == "":
			playlist_name = self.default_playlist_name
		return os.path.join(self.sd_path, playlist_name)

	def add_song(self, playlist_name, source_song_path, song_name, multi_speed):
		playlist_name = self.default_playlist_name if playlist_name == "" else playlist_name
		playlist_path = self._get_playlist_path(playlist_name)
		song_path = os.path.join(playlist_path, song_name)
	
		if not os.path.isdir(playlist_path):
			print(f"❌ ERROR: Playlist '{playlist_name}' not found.")
			return
		elif not source_song_path.lower().endswith(".mp3") or not os.path.isfile(source_song_path):
			print(f"❌ ERROR: '{source_song_path}' is an invalid MP3 file path.")
			return
		elif self._is_song_in_playlist(song_name, playlist_path):
			print(f"❌ ERROR: Already exists a song with the name '{song_name}' in this playlist.")
			return

		print(f"⏳ Processing '{song_name}'... Please wait.")
		os.makedirs(song_path)
		try:
			audio = AudioSegment.from_file(source_song_path)

			if multi_speed.lower() == "y":
				for speed in self.speeds:
					new_rate = int(audio.frame_rate * speed)
					new_audio = audio._spawn(audio.raw_data, overrides = {'frame_rate': new_rate})
					new_audio = new_audio.set_frame_rate(audio.frame_rate)
					new_audio.export(os.path.join(song_path, f"{speed}x.mp3"), format = "mp3")
			else:
				audio.export(os.path.join(song_path, "1.0x.mp3"), format = "mp3")
			
			print(f"✅ SUCCESS: '{song_name}' added to '{playlist_name}'.")
		except Exception as e:
			print(f"❌ ERROR: {e}")
			shutil.rmtree(song_path)
		
	def delete_song(self, playlist_name, song_name):
		playlist_name = self.default_playlist_name if playlist_name == "" else playlist_name
		playlist_path = self._get_playlist_path(playlist_name)
		song_path = os.path.join(playlist_path, song_name)

		if not os.path.isdir(playlist_path):
			print(f"❌ ERROR: Playlist '{playlist_name}' not found.")
			return
		elif not os.path.isdir(song_path):
			print(f"❌ ERROR: Song '{song_name}' not found in '{playlist_name}'.")
			return

		shutil.rmtree(song_path)
		print(f"✅ SUCCESS: Song '{song_name}' from playlist '{playlist_name}' deleted.")

	def create_playlist(self, playlist_name):
		playlist_path = self._get_playlist_path(playlist_name)

		if not playlist_path:
			print("❌ ERROR: Playlist name cannot be empty.")
			return
		elif playlist_name == self.default_playlist_name:
			print(f"❌ ERROR: '{self.default_playlist_name}' is a protected system folder.")
			return
		elif os.path.isdir(playlist_path):
			print(f"❌ ERROR: Playlist '{playlist_name}' already exists.")
			return

		os.makedirs(os.path.join(self.sd_path, playlist_name))
		print(f"✅ SUCCESS: Playlist '{playlist_name}' created.")

	def delete_playlist(self, playlist_name):
		playlist_path = self._get_playlist_path(playlist_name)

		if playlist_name == "" or playlist_name == self.default_playlist_name:
			print(f"❌ ERROR: The default playlist '{self.default_playlist_name}' cannot be deleted.")
			return
		if not os.path.isdir(playlist_path):
			print(f"❌ ERROR: Playlist '{playlist_name}' not found.")
			return

		shutil.rmtree(playlist_path)
		print(f"✅ SUCCESS: Playlist '{playlist_name}' deleted.")
	
	def show_playlist(self, playlist_name):
		playlist_name = self.default_playlist_name if playlist_name == "" else playlist_name
		playlist_path = self._get_playlist_path(playlist_name)

		if not os.path.isdir(playlist_path):
			print(f"❌ ERROR: Playlist '{playlist_name}' not found.")
			return

		tag = " [DEFAULT]" if playlist_name == self.default_playlist_name else ""
		print(f"\n📂 {playlist_name.upper()} {tag}")
		
		playlist_path = os.path.join(self.sd_path, playlist_name)
		songs_names = [s for s in os.listdir(playlist_path)]
		
		if not songs_names:
			print("   └─ (Empty Playlist)")
		else:
			for s in songs_names:
				song_path = os.path.join(playlist_path, s)
				files = [f for f in os.listdir(song_path)]
				multi = "YES" if len(files) > 1 else "NO"
				
				print(f"   └─ 🎵 {s:<40} | Multi-Speed: {multi}")

	def show_all_playlists(self):
		playlists_names = [p for p in os.listdir(self.sd_path)]

		if not playlists_names:
			print(" (The SD card is empty)")
			return

		for p in playlists_names:
			self.show_playlist(p)

		

# --- ENTRY POINT ---

def main():
	print("====================================")
	print(" TRACK MANAGER APP")
	print("====================================")
	
	check_dependencies()
	sd_path = initialize_sd()
	manager = TrackManager("/media/necula-mihail/A637-9142") #sd_path)

	while True:
		print("\n" + "="*50)
		print(" ACTIONS: ADD SONG | DELETE SONG | CREATE PLAYLIST")
		print("          DELETE PLAYLIST | SHOW PLAYLIST")
		print("          SHOW ALL PLAYLISTS | EXIT")
		print("="*50)
		cmd = input("\nSELECT ACTION: ").strip().upper()

		if cmd == "ADD SONG":
			playlist_name = input("Target Playlist Name (Press Enter for Default): ").strip().upper()
			source_song_path = input("Source MP3 Path: ").strip().strip('"')
			song_name = input("Save as Song Name: ").strip().upper()
			multi_speed = input("Enable Multi-Speed (Press y for yes): ").strip().upper()

			manager.add_song(playlist_name, source_song_path, song_name, multi_speed)

		elif cmd == "DELETE SONG":
			playlist_name = input("Playlist Name (Press Enter for Default): ").strip().upper()
			song_name = input("Song Name to Delete: ").strip().upper()

			manager.delete_song(playlist_name, song_name)
		
		elif cmd == "CREATE PLAYLIST":
			playlist_name = input("New Playlist Name: ").strip().upper()

			manager.create_playlist(playlist_name)
	
		elif cmd == "DELETE PLAYLIST":
			playlist_name = input("Playlist Name to Delete: ").strip().upper()

			manager.delete_playlist(playlist_name)

		elif cmd == "SHOW PLAYLIST":
			playlist_name = input("Playlist Name (Press Enter for Default): ").strip().upper()

			manager.show_playlist(playlist_name)
		elif cmd == "SHOW ALL PLAYLISTS":
			manager.show_all_playlists()

		elif cmd == "EXIT":
			print("Shutting down Track Manager. Goodbye!")
			break

		else:
			print("🚨 Invalid command. Please try again.")

if __name__ == "__main__":
	main()