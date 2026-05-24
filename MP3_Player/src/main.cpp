#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <U8g2lib.h>
#include <Wire.h>
#include "DFRobotDFPlayerMini.h"
#include <BH1750.h> 

/* --- INPUTS (Buttons & Encoder) --- */
#define ENC_CLK    PE4
#define ENC_DT     PE5
#define ENC_SW     PG5
#define SW_1   	   PE3
#define SW_2       PH3
#define SW_3       PH4

/* --- OUTPUTS (Metadata SD & LEDs) --- */
#define HW_SS          PB0
#define LED_R      	   PH5
#define LED_G          PH6
#define LED_B          PB4

#define SD_CS_PIN      53

/* --- I2C --- */
#define SCL 		   PD0
#define SDA			   PD1

/* --- UART --- */
#define RX1 		   PD2
#define TX1			   PD3

/* --- OTHERS --- */
#define MAX_NAME_LEN        50
#define MAX_PATH_LEN        11
#define MAX_DURATION_LEN    11
#define MAX_LINE_LEN        (MAX_NAME_LEN + 10)
#define TEXT_X_START        4
#define MAX_TEXT_WIDTH      (128 - TEXT_X_START)

/* --- GLOBAL VARIABLES --- */
DFRobotDFPlayerMini musicPlayer;
U8G2_SH1106_128X64_NONAME_F_HW_I2C screen(/* rotation=*/ U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
BH1750 lightSensor; 

enum PlayerState { STATE_SELECT_PLAYLIST, STATE_SELECT_SONG, STATE_SONG_SELECTED };
PlayerState currState = STATE_SELECT_PLAYLIST;

int totalPlaylists 						= 0; 
int currPlaylistIdx 					= 0; 
int playlistsOffset 					= 0;
int currSongIdx 						= 0;     
int songsOffset							= 0;
int totalSongsInPlaylist 				= 0;

const char* speedTexts[] 				= {"0.50x", "0.75x", "1.00x", "1.25x", "1.50x"};
const float speedValues[] 				= {0.50f, 0.75f, 1.00f, 1.25f, 1.50f};
int currSpeedIdx 						= 2;

int volume 								= 10;
unsigned long lastVolumeUpdate 			= 0;
bool volumeNeedsSaving 					= false;

bool isPlaying 							= false;
char currPlaylistName[MAX_NAME_LEN + 1] = "";  
char currSongName[MAX_NAME_LEN + 1] 	= "";  
int currBaseSongTime 					= 0; 	/* seconds */
int currSongTime 						= 0;    /* seconds */
int elapsedTime 						= 0;    /* seconds */
unsigned long lastTimeBarUpdate 		= 0;	/* milliseconds */

volatile int currEncoderPos 			= 0;
int prevEncoderPos 						= 0;
unsigned long lastEncoderPress			= 0;     /* milliseconds  */
int pressedEncoder						= 0;

unsigned long lastSW1Press				= 0;     /* milliseconds  */
unsigned long lastSW2Press				= 0;     /* milliseconds  */
unsigned long lastSW3Press				= 0;     /* milliseconds  */

int marqueeX 							= 4;                
unsigned long lastMarqueeTick 			= 0;	/* milliseconds */
int prevIdx								= -1;
unsigned long marqueeWaitUntil			= 0; 	/* miliseconds */

bool autoBrightness						= false;
unsigned long lastBrightnessUpdate		= 0;
float currLux							= 500.0f; 

bool comboFiredSW_1_2 					= false;
bool forceFullRedraw					= true; 

// =========================================================
// === GPIO, TIMERS and INTERRUPTS INITIALIZATION ===
// =========================================================

void setupInputs() {
	DDRE &= ~(_BV(SW_1) | _BV(ENC_CLK) | _BV(ENC_DT)); 
	PORTE |= _BV(SW_1);                         
	PORTE &= ~(_BV(ENC_CLK) | _BV(ENC_DT));           

	DDRG &= ~_BV(ENC_SW); 
	PORTG |= _BV(ENC_SW); 

	DDRH &= ~(_BV(SW_2) | _BV(SW_3)); 
	PORTH |= (_BV(SW_2) | _BV(SW_3)); 
}

void setupOutputs() {
	DDRB |= (_BV(HW_SS) | _BV(LED_B)); /* HW_SS set as output => we are the master */
	PORTB &= ~_BV(LED_B);

	DDRH |= (_BV(LED_R) | _BV(LED_G)); 
	PORTH &= ~(_BV(LED_R) | _BV(LED_G));
}

void setupTimersPwm() {
	/* Timer 4 (16-bit) - Fast PWM 8-bit, Prescaler = 8, Switch LED_R at threshold */
	TCCR4A = _BV(COM4C1) | _BV(WGM40);
	TCCR4B = _BV(WGM42)  | _BV(CS41);
	OCR4C  = 0; /* LED_R threshold */

	/* Timer 2 (8-bit) - Fast PWM complete, Prescaler = 8, Switch LED_G and LED_B at thresholds */
	TCCR2A = _BV(COM2A1) | _BV(COM2B1) | _BV(WGM21) | _BV(WGM20);
	TCCR2B = _BV(CS21);
	OCR2B  = 0; /* LED_G threshold */
	OCR2A  = 0; /* LED_B threshold */
}

void setupExternalInterrupts() {
	cli();
	
	/* INT4 for the encoder rotation */
	EICRB |= _BV(ISC40); /* Change Mode */
	EIMSK |= _BV(INT4);  /* Activate locally the interrupt */
	
	sei();
}

ISR(INT4_vect) {
	static unsigned long lastInterruptTime = 0;
	unsigned long interruptTime;
	
	interruptTime = millis();
	if (interruptTime - lastInterruptTime > 5) {
		bool clkState = (PINE & _BV(ENC_CLK));
		bool dtState  = (PINE & _BV(ENC_DT));
		
		if (clkState != dtState) {  /* DT has latency */
			currEncoderPos++;
		} else {                    /* CLK has latency */
			currEncoderPos--;
		}
	}
	lastInterruptTime = interruptTime;
}

// =========================================================
// === METADATA MicroSD ===
// =========================================================

int countPlaylists() {
	int count;
	char path[MAX_PATH_LEN + 1];
	
	count = 0;
	while (true) {
		snprintf(path, sizeof(path), "/%02d/000.txt", count + 1);
		if (SD.exists(path)) {
			count++;
		} else {
			break;
		}
	}
	return count;
}

int countSongs(int playlistIdx) {
	int count;
	char path[MAX_PATH_LEN + 1];
	
	count = 0;
	while (true) {
		snprintf(path, sizeof(path), "/%02d/%03d.txt", playlistIdx + 1, count + 1);
		if (SD.exists(path)) {
			count++;
		} else {
			break;
		}
	}
	return count;
}

void saveSettingsToSD() {
	File configFile;
	char buffer[4];
	
	if (SD.exists("/config.txt")) {
		SD.remove("/config.txt");
	}

	configFile = SD.open("/config.txt", FILE_WRITE);    
	if (configFile) {
		snprintf(buffer, sizeof(buffer), "%02d%d", volume, autoBrightness ? 1 : 0);
		configFile.print(buffer);
		configFile.close();
	}
}

void loadSettingsFromSD() {
	File configFile;
	char v1, v2, b1;

	if (!SD.begin(SD_CS_PIN)) {
		screen.clearBuffer();
		screen.drawUTF8(10, 30, "SD Card Error!");
		screen.sendBuffer();

		while(1) {
			OCR4C = 10; delay(100);
			OCR4C = 0; delay(100); 
		}
	}

	currState = STATE_SELECT_PLAYLIST;
	currPlaylistIdx = 0;
	playlistsOffset = 0;
	currSongIdx = 0; 
	songsOffset = 0;
	currSpeedIdx = 2;
	isPlaying = false;
	volume = 10;
	autoBrightness = false;

	totalPlaylists = countPlaylists();
	totalSongsInPlaylist = countSongs(currPlaylistIdx);
	playlistsOffset = (currPlaylistIdx >= 3) ? currPlaylistIdx - 2 : 0;
	songsOffset = (currSongIdx >= 4) ? currSongIdx - 3 : 0;

	if (!SD.exists("/config.txt")) {
		saveSettingsToSD(); 
		return;
	}

	configFile = SD.open("/config.txt", FILE_READ);
	if (configFile) {
		if (configFile.available() >= 3) {
			v1 = configFile.read(); 
			v2 = configFile.read(); 
			b1 = configFile.read(); 

			volume = ((v1 - '0') * 10) + (v2 - '0');
			autoBrightness = (b1 == '1');

			if (volume < 0 || volume > 30) {
				volume = 10;
			}
		}
		configFile.close();
		delay(200);
	}
}

void getPlaylistName(int playlistIdx, char* name) {
	char path[MAX_PATH_LEN + 1];
	File playlistFile;
	int len;

	snprintf(path, sizeof(path), "/%02d/000.txt", playlistIdx + 1);
	playlistFile = SD.open(path);
	if (!playlistFile) {
		snprintf(name, MAX_NAME_LEN, "%02d", playlistIdx + 1);
		return;
	}
	
	len = playlistFile.readBytesUntil('\n', name, MAX_NAME_LEN);
	name[len] = '\0';
	if (name[len - 1] == '\r') {
		name[len - 1] = '\0';
	}

	playlistFile.close();
}

void getSongMetadata(int playlistIdx, int songIdx, char* name, int& duration) {
	char path[MAX_PATH_LEN + 1], buffer[MAX_DURATION_LEN + 1];
	File songFile;
	int nameLen, durationLen, mins, secs;

	snprintf(path, sizeof(path), "/%02d/%03d.txt", playlistIdx + 1, songIdx + 1);
	songFile = SD.open(path);
	if (!songFile) {
		snprintf(name, MAX_NAME_LEN, "%03d", songIdx + 1);
		return;
	}

	nameLen = songFile.readBytesUntil('\n', name, MAX_NAME_LEN);
	name[nameLen] = '\0';
	if (name[nameLen - 1] == '\r') {
		name[nameLen - 1] = '\0';
	}

	durationLen = songFile.readBytesUntil('\n', buffer, MAX_DURATION_LEN);
	buffer[durationLen] = '\0';
	if (buffer[durationLen - 1] == '\r') {
		buffer[durationLen - 1] = '\0';
	}
	songFile.close();

	if (sscanf(buffer, "%d:%d", &mins, &secs) == 2) {
		duration = (mins * 60) + secs;
	} else {
		duration = 0;
	}
}

void computeCurrSongTime() {
	if (currBaseSongTime > 0) {
		currSongTime = round((float)currBaseSongTime / speedValues[currSpeedIdx]);
	} else {
		currSongTime = 0;
	}
}

// =========================================================
// === AUDIO MicroSD ===
// =========================================================

void setupPlayer() {
	DDRD |= _BV(TX1);
	DDRD &= ~_BV(RX1);
	PORTD |= _BV(RX1);

	// UBRR = (F_CPU / (16 * Baud)) - 1
	// (16 000 000 / (16 * 9600)) - 1 = 103.16 => 103
	UBRR1 = 103;
	UCSR1A = 0x00;
	UCSR1B = _BV(RXEN1) | _BV(TXEN1);
	UCSR1C = _BV(UCSZ11) | _BV(UCSZ10);

	delay(50);
	Serial1.begin(9600);
	if (musicPlayer.begin(Serial1)) {
		musicPlayer.volume(volume);
		delay(200);
	}
}

void playSong() {
	int targetFolder, targetTrack;

	targetFolder = (currPlaylistIdx * 5) + (currSpeedIdx + 1); 
	targetTrack = currSongIdx + 1; 
	
	musicPlayer.playFolder(targetFolder, targetTrack);
	delay(100); 
	if (isPlaying) {         /* Need to pause and start at begining to be sure that the song started. */
		musicPlayer.pause(); 
		delay(50);
		musicPlayer.start();
	} else {
		musicPlayer.pause(); 
	}
	lastTimeBarUpdate = millis();
}

// =========================================================
// === LIGHT SENSOR ===
// =========================================================

void setupLightSensor() {
	lightSensor.begin();
}

void updateBrightness() {
	int contrast;

	if (millis() - lastBrightnessUpdate >= 500) {
		lastBrightnessUpdate = millis();
		currLux = lightSensor.readLightLevel();
		
		if (autoBrightness) {
			contrast = map((int)constrain(currLux, 0, 1000), 0, 1000, 20, 255);
			screen.setContrast(contrast);
		} else {
			screen.setContrast(255);
		}
	}
}

// =========================================================
// === RGB LED ===
// =========================================================

void updateLED() {
	switch (currState) {
		case STATE_SELECT_PLAYLIST:
			OCR4C = 0;  /* LED_R */
			OCR2B = 2;  /* LED_G */
			OCR2A = 3;  /* LED_B */
			break;
		case STATE_SELECT_SONG:
			OCR4C = 4;
			OCR2B = 1;
			OCR2A = 0;  
			break;
		case STATE_SONG_SELECTED:
			OCR4C = 3;
			OCR2B = 0;
			OCR2A = 3;  
			break;
	}
}

// =========================================================================
// === GRAPHIC INTERFACE (OLED) ===
// =========================================================================

void setupOLED() {
	screen.begin();
	screen.enableUTF8Print(); 
	screen.setDrawColor(1);
}

void startMarquee(int currIdx) {
	marqueeX = TEXT_X_START;
	marqueeWaitUntil = millis() + 1000;
	lastMarqueeTick = millis();
	prevIdx = currIdx;
}

void updateMarquee(int currIdx, int textWidth) {
	if (textWidth > 124) { 
		if (marqueeWaitUntil == 0 && millis() - lastMarqueeTick >= 12) {
			marqueeX--;
			lastMarqueeTick = millis();

			if (marqueeX <= -textWidth)	{
				startMarquee(currPlaylistIdx);
			}
		} else if (millis() >= marqueeWaitUntil) {
			marqueeWaitUntil = 0;
			lastMarqueeTick = millis();
		}
	}
}

void drawPlaylistScreen() {
	int playlistIdx, visualIdx, yPos, textWidth;
	char name[MAX_NAME_LEN + 1], line[MAX_LINE_LEN + 1];

	if (forceFullRedraw) {
		screen.clearBuffer();

		// Top of screen
		screen.setDrawColor(1); /* White */
		screen.drawUTF8(TEXT_X_START + 1, 12, "=== Playlists ===");

		if (autoBrightness) {
			screen.drawUTF8(118, 12, "A");
		}

		screen.drawHLine(0, 15, 128);

		// Content - max 3 playlists - exclude the selected playlist
		for (int i = 0; i < 3; i++) {
			playlistIdx = playlistsOffset + i;

			if (playlistIdx >= totalPlaylists) {
				break;
			}

			if (playlistIdx == currPlaylistIdx) {
				continue;
			}

			getPlaylistName(playlistIdx, name);
			snprintf(line, sizeof(line), "%02d. %s", playlistIdx + 1, name);

			screen.setDrawColor(1);
			screen.drawUTF8(TEXT_X_START, 18 + (i * 14) + 11, line);
		}

		forceFullRedraw = false;
	}

	// Selection changed
	if (currPlaylistIdx != prevIdx) {
		startMarquee(currPlaylistIdx);
	}

	// Get playlist visual position
	visualIdx = currPlaylistIdx - playlistsOffset;
	yPos = 18 + (visualIdx * 14);

	// Get playlist text
	getPlaylistName(currPlaylistIdx, currPlaylistName);
	snprintf(line, sizeof(line), "%02d. %s", currPlaylistIdx + 1, currPlaylistName);
	textWidth = screen.getUTF8Width(line);

	// Marquee
	updateMarquee(currPlaylistIdx, textWidth);

	// Draw selected playlist
	screen.setClipWindow(0, yPos, 128, yPos + 14);
	screen.setDrawColor(1); screen.drawBox(0, yPos, 128, 14);
	screen.setDrawColor(0); screen.drawUTF8(marqueeX, yPos + 11, line);

	// Send all to drawing
	screen.sendBuffer(); screen.setMaxClipWindow();
}

void drawSongScreen() {
	int songIdx, visualIdx, yPos, textWidth, duration;
	char name[MAX_NAME_LEN + 1], line[MAX_LINE_LEN + 1];

	if (forceFullRedraw) {
		screen.clearBuffer();

		// Top of screen
		screen.setDrawColor(1);
		screen.drawUTF8(TEXT_X_START + 1, 12, "=== Songs ===");

		if (autoBrightness) {
			screen.drawUTF8(118, 12, "A");
		}

		screen.drawHLine(0, 15, 128);

		// Content - max 4 songs - exclude the selected song
		for (int i = 0; i < 4; i++) {
			songIdx = songsOffset + i;

			if (songIdx >= totalSongsInPlaylist) {
				break;
			}

			if (songIdx == currSongIdx) {
				continue;
			}

			getSongMetadata(currPlaylistIdx, songIdx, name, duration);
			snprintf(line, sizeof(line), "%03d. %s", songIdx + 1, name);

			screen.setDrawColor(1);
			screen.drawUTF8(TEXT_X_START, 18 + (i * 11) + 9, line);
		}

		forceFullRedraw = false;
	}

	// Selection changed
	if (currSongIdx != prevIdx) {
		startMarquee(currSongIdx);
	}

	// Selected song visual position
	visualIdx = currSongIdx - songsOffset;
	yPos = 18 + (visualIdx * 11);

	// Selected song text
	getSongMetadata(currPlaylistIdx, currSongIdx, name, duration);
	snprintf(line, sizeof(line), "%03d. %s", currSongIdx + 1, name);
	textWidth = screen.getUTF8Width(line);

	// Marquee
	updateMarquee(currSongIdx, textWidth);

	// Draw selected song
	screen.setClipWindow(0, yPos, 128, yPos + 11);
	screen.setDrawColor(1);	screen.drawBox(0, yPos, 128, 11);
	screen.setDrawColor(0);	screen.drawUTF8(marqueeX, yPos + 9, line);

	// Send all to drawing
	screen.sendBuffer(); screen.setMaxClipWindow();
}

void drawPlayerScreen() {
	char buffer[MAX_LINE_LEN + 1];
	int fillWidth;

	if (forceFullRedraw) {
		screen.clearBuffer();
		
		// Playlist name
		screen.setClipWindow(0, 0, 128, 14);
		screen.setDrawColor(1); screen.drawUTF8(5, 12, currPlaylistName);
		screen.setMaxClipWindow();

		// Song name
		snprintf(buffer, sizeof(buffer), "%03d. %s", currSongIdx + 1, currSongName);
		screen.setClipWindow(0, 15, 128, 27);
		screen.setDrawColor(1); screen.drawUTF8(5, 24, buffer);
		screen.setMaxClipWindow();

		// Divider
		screen.drawHLine(0, 29, 128);
	}

	// Time Bar
	screen.drawFrame(5, 33, 118, 6);
	fillWidth = 0;
	if (currSongTime > 0) {
		fillWidth = (elapsedTime * 118) / currSongTime;
		if (fillWidth > 118) {
			fillWidth = 118;
		}
	}
	screen.drawBox(5, 33, fillWidth, 6);
	
	// Time
	snprintf(buffer, sizeof(buffer), "%02d:%02d/%02d:%02d", elapsedTime / 60, elapsedTime % 60,
		currSongTime / 60, currSongTime % 60);

	screen.setClipWindow(0, 41, 90, 52);
	screen.setDrawColor(0); screen.drawBox(0, 41, 90, 11); 
	screen.setDrawColor(1); screen.drawUTF8(5, 50, buffer);
	screen.setMaxClipWindow();

	// Speed 
	screen.setClipWindow(90, 41, 128, 52);
	screen.setDrawColor(0); screen.drawBox(90, 41, 38, 11); 
	screen.setDrawColor(1); screen.drawUTF8(93, 50, speedTexts[currSpeedIdx]);
	screen.setMaxClipWindow();

	// Volume
	snprintf(buffer, sizeof(buffer), "Vol: %d/30", volume);

	screen.setClipWindow(0, 53, 80, 64);
	screen.setDrawColor(0); screen.drawBox(0, 53, 80, 11);
	screen.setDrawColor(1); screen.drawUTF8(5, 62, buffer);
	screen.setMaxClipWindow();

	// Play / Pause Button
	screen.setClipWindow(108, 53, 128, 64);
	screen.setDrawColor(0); screen.drawBox(108, 53, 20, 11);
	
	screen.setDrawColor(1);
	if (isPlaying) {
		screen.drawBox(113, 54, 3, 9);
		screen.drawBox(119, 54, 3, 9); } 
	else {
		screen.drawTriangle(114, 54, 114, 63, 122, 58);
	}
	
	screen.setMaxClipWindow();

	// Ready
	screen.sendBuffer(); 
}

void drawInterface() {
	if (forceFullRedraw) {
		screen.clearBuffer();
	}
	screen.setFont(u8g2_font_6x12_tf); 

	if (currState == STATE_SELECT_PLAYLIST) {
		drawPlaylistScreen();
	} else if (currState == STATE_SELECT_SONG) {
		drawSongScreen();
	} else if (currState == STATE_SONG_SELECTED) {
		drawPlayerScreen();
	}
}

// =========================================================================
// === I2C ===
// =========================================================================

void setupI2C() {
	// Configure SCL and SDA as inputs and enable internal pull-ups
	DDRD  &= ~(_BV(SCL) | _BV(SDA)); 
	PORTD |=  (_BV(SCL) | _BV(SDA));

	// Set the I2C Prescaler to 1
	TWSR &= ~(_BV(TWPS1) | _BV(TWPS0));

	// Set Bit Rate Register to 12 for 400kHz clock speed
	// SCL_Frequency = F_CPU / (16 + 2 * TWBR * (4 ^ TWPS))
	// 16MHz / (16 + 2 * 12 * 1) = 400kHz
	TWBR = 12;

	// Enable I2C 
	TWCR = _BV(TWEN);
}

// =========================================================================
// === ARDUINO CORE FUNCTIONS (SETUP & LOOP) ===
// =========================================================================

void setup() {
	setupInputs();
	setupOutputs();
	setupTimersPwm();
	setupExternalInterrupts();

	Wire.begin();
	Wire.setClock(400000);
	setupLightSensor();
	setupOLED();
	loadSettingsFromSD();
	setupPlayer();
	
	forceFullRedraw = true;
	drawInterface();
}

void loop() {
	bool b1, b2, b3, be;

	updateBrightness();
	updateLED();
	drawInterface();

	// SW1 + SW2 (Previous Menu)
	b1 = !(PINE & _BV(SW_1)); 
	b2 = !(PINH & _BV(SW_2)); 
	if ((b1 || b2) && !comboFiredSW_1_2) {
		delay(50); 
		b1 = !(PINE & _BV(SW_1));
		b2 = !(PINH & _BV(SW_2));

		if (b1 && b2) {
			comboFiredSW_1_2 = true;
 
			if (currState == STATE_SONG_SELECTED) {
				if (isPlaying) {
					musicPlayer.pause();
					isPlaying = false;
				}
				currState = STATE_SELECT_SONG; 
			} else if (currState == STATE_SELECT_SONG) {
				currState = STATE_SELECT_PLAYLIST; 
			}

			forceFullRedraw = true;
			drawInterface();
		}
	} else if (!b1 && !b2) {
		comboFiredSW_1_2 = false;
	}

	// Volume Change
	if (currEncoderPos != prevEncoderPos) {
		if (currEncoderPos > prevEncoderPos) {
			volume = min(volume + 1, 30); 
		} else { 
			volume = max(volume - 1, 0);
		}
		
		musicPlayer.volume(volume);
		prevEncoderPos = currEncoderPos;
		volumeNeedsSaving = true;
		lastVolumeUpdate = millis();

		if (currState == STATE_SONG_SELECTED) {
			drawInterface();
		}
	} else if (volumeNeedsSaving && (millis() - lastVolumeUpdate >= 1000)) {
		volumeNeedsSaving = false;
		saveSettingsToSD();
	}
	
	// SW1 pressed + SW2 free (UP / LEFT Navigation)
	b1 = !(PINE & _BV(SW_1)); 
	b2 = !(PINH & _BV(SW_2)); 
	if (b1 && !b2 && millis() - lastSW1Press > 200) {
    	lastSW1Press = millis();

		if (currState == STATE_SELECT_PLAYLIST) {

			if (currPlaylistIdx > 0) { 
				currPlaylistIdx--;
				if (currPlaylistIdx < playlistsOffset) {
					playlistsOffset--;
				}
			} else {
				currPlaylistIdx = totalPlaylists - 1;
				playlistsOffset = (totalPlaylists > 3) ? totalPlaylists - 3 : 0;
			}

		} else if (currState == STATE_SELECT_SONG) {

			if (currSongIdx > 0) { 
				currSongIdx--;
				if (currSongIdx < songsOffset) {
					songsOffset--;
				}
			} else {
				currSongIdx = totalSongsInPlaylist - 1;
				songsOffset = (totalSongsInPlaylist > 4) ? totalSongsInPlaylist - 4 : 0;
			}

		} else if (currState == STATE_SONG_SELECTED) {

			if (currSongIdx > 0) { 
				currSongIdx--;
				if (currSongIdx < songsOffset) {
					songsOffset--;
				}
			} else {
				currSongIdx = totalSongsInPlaylist - 1;
				songsOffset = (totalSongsInPlaylist > 4) ? totalSongsInPlaylist - 4 : 0;
			}

			getSongMetadata(currPlaylistIdx, currSongIdx, currSongName, currBaseSongTime);
			computeCurrSongTime();
			elapsedTime = 0;
			playSong();
		}

		forceFullRedraw = true;
		drawInterface();
	}
	
	// SW3 pressed (DOWN / RIGHT Navigation)
	b3 = !(PINH & _BV(SW_3));
	if (b3 && millis() - lastSW3Press > 200) {
		lastSW3Press = millis();

		if (currState == STATE_SELECT_PLAYLIST) {

			if (currPlaylistIdx < totalPlaylists - 1) {
				currPlaylistIdx++;
				if (currPlaylistIdx >= playlistsOffset + 3) {
					playlistsOffset++;
				}
			} else {
				currPlaylistIdx = 0;
				playlistsOffset = 0;
			}

		} else if (currState == STATE_SELECT_SONG) {

			if (currSongIdx < totalSongsInPlaylist - 1) {
				currSongIdx++;
				if (currSongIdx >= songsOffset + 4) {
					songsOffset++;
				}
			} else {
				currSongIdx = 0;
				songsOffset = 0;
			}

		} 
		else if (currState == STATE_SONG_SELECTED) {

			if (currSongIdx < totalSongsInPlaylist - 1) {
				currSongIdx++;
				if (currSongIdx >= songsOffset + 4) {
					songsOffset++;
				}
			} else {
				currSongIdx = 0;
				songsOffset = 0;
			}

			getSongMetadata(currPlaylistIdx, currSongIdx, currSongName, currBaseSongTime);
			computeCurrSongTime();
			elapsedTime = 0;
			playSong();
		}

		forceFullRedraw = true;
		drawInterface();
	}
	
	// SW1 free + SW2 pressed (OK / PLAY / PAUSE)
	b1 = !(PINE & _BV(SW_1)); 
	b2 = !(PINH & _BV(SW_2)); 
	if (b2 && !b1 && millis() - lastSW2Press > 200) {
    	lastSW2Press = millis();

		if (currState == STATE_SELECT_PLAYLIST) {

			currState = STATE_SELECT_SONG;
			totalSongsInPlaylist = countSongs(currPlaylistIdx);
			currSongIdx = 0;
			songsOffset = 0;
			startMarquee(currPlaylistIdx);

		} else if (currState == STATE_SELECT_SONG) {

			currState = STATE_SONG_SELECTED;
			getSongMetadata(currPlaylistIdx, currSongIdx, currSongName, currBaseSongTime);
			computeCurrSongTime();
			elapsedTime = 0;
			isPlaying = true;
			playSong();
			startMarquee(currSongIdx);

		} else if (currState == STATE_SONG_SELECTED) {

			if (isPlaying) {
				musicPlayer.pause();
				isPlaying = false;
			} else {
				musicPlayer.start();
				isPlaying = true;
			}
		}

		forceFullRedraw = true;
		drawInterface();
	}

	// ENC SW pressed (Automate Brightness ON and OFF / Speed Change )
	be = !(PING & _BV(ENC_SW));
	if (be && millis() - lastEncoderPress > 200 && !pressedEncoder) {
		lastEncoderPress = millis();
		pressedEncoder = 1;

		if (currState == STATE_SELECT_PLAYLIST || currState == STATE_SELECT_SONG) {
			autoBrightness = !autoBrightness;
			screen.setContrast(255); 
		} else if (currState == STATE_SONG_SELECTED) {
			currSpeedIdx = (currSpeedIdx >= 4) ? 0 : currSpeedIdx + 1;
			computeCurrSongTime();
			elapsedTime = 0;
			playSong();                      
		}

		forceFullRedraw = true;
		saveSettingsToSD();
		drawInterface();
	} else {
		pressedEncoder = 0;
	}
	
	// Update Time Bar
	if (currState == STATE_SONG_SELECTED && isPlaying && millis() - lastTimeBarUpdate >= 1000) {
		lastTimeBarUpdate = millis();
		elapsedTime++;
	
		if (elapsedTime > currSongTime) {
			currSongIdx = (currSongIdx + 1 >= totalSongsInPlaylist) ? 0 : currSongIdx + 1;
			elapsedTime = 0; 
			getSongMetadata(currPlaylistIdx, currSongIdx, currSongName, currBaseSongTime);
			computeCurrSongTime();
			playSong();
		}

		forceFullRedraw = true; drawInterface();
	}
}