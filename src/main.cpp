#include <Arduino.h>
// Explicitly include core components without networking
#include <HardwareSerial.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_PN532.h>

#include "melodies.h"
#include "nfc.h"
#include "serialize.h"
#include "logging.h"

#include "main.h"

// Ensure core Arduino objects are available
extern HardwareSerial Serial;
extern SPIClass SPI;
extern TwoWire Wire;

// Pin definitions for Wemos D1 Mini
#define PN532_SS   (16)  // D0 - Slave Select pin for PN532

// Use hardware SPI communication for PN532
// Hardware SPI uses fixed pins: SCK=D5, MOSI=D7, MISO=D6
Adafruit_PN532 nfc(PN532_SS);

// System states
enum RaceState {
  RACE_PENDING,
  RACE_RUNNING
};

// Global variables
RaceState currentState = RACE_PENDING;
CheckpointPress pressTable[100];  // Max 100 checkpoint presses
uint8_t pressCount = 0;
uint32_t lastNfcCheck = 0;
uint32_t raceStartTime = 0;  // Timestamp in milliseconds when KOR00 was scanned (race start)
const uint32_t NFC_CHECK_INTERVAL = 500; // Check NFC every 500ms

// Function declarations
void processCheckpoint(uint8_t checkpointNum);
void processReadoutTrigger();
void clearPressTable();
void addCheckpointPress(uint8_t checkpoint, bool isStart);
void printPressTable();

void setup() {
  Serial.begin(115200);
  LOGLN_INFO(F("KOR Orienteering Checkpoint Tracker"));
  
  // Initialize buzzer pin
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Play startup tone
  playMelody(INIT_MELODY, INIT_MELODY_LENGTH);
  
  // Initialize PN532
  nfc.begin();
  
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    LOGLN_ERROR(F("Didn't find PN532 board"));
    delay(500);
    playMelody(ERROR_MELODY, ERROR_MELODY_LENGTH);
    delay(500);
    playMelody(ERROR_MELODY, ERROR_MELODY_LENGTH);
    delay(500);
    playMelody(ERROR_MELODY, ERROR_MELODY_LENGTH);
    while (1) delay(1000); // halt
  }
  
  LOG_INFO(F("Found chip PN5"));
  LOGLN_INFO((versiondata>>24) & 0xFF, HEX);
  LOG_INFO(F("Firmware ver. "));
  LOG_INFO((versiondata>>16) & 0xFF, DEC);
  LOG_INFO('.');
  LOGLN_INFO((versiondata>>8) & 0xFF, DEC);
  
  // Configure for reading NTAG213/215/216
  nfc.SAMConfig();
  
  LOGLN_INFO(F("System ready - PENDING state"));
  LOGLN_INFO(F("Present KOR00 to start tracking"));
}

void loop() {
  uint32_t currentTime = millis();
  
  // Check for NFC card periodically
  if (currentTime - lastNfcCheck >= NFC_CHECK_INTERVAL) {
    lastNfcCheck = currentTime;
    readNfcCard();
  }
  
  delay(10); // Small delay to prevent excessive CPU usage
}

void processCheckpoint(uint8_t checkpointNum) {
  bool validCheckpoint = false;
  
  if (currentState == RACE_PENDING) {
    if (checkpointNum == 0) {
      LOGLN_INFO(F("Start checkpoint detected - clearing table and switching to RUNNING"));
      clearPressTable();  // Clear everything first
      raceStartTime = millis();  // Set race start time baseline in milliseconds
      LOG_DEBUG(F("Race start time set to: "));
      LOGLN_DEBUG(raceStartTime);
      addCheckpointPress(0, true);
      currentState = RACE_RUNNING;
      validCheckpoint = true;
      playMelody(INIT_MELODY, INIT_MELODY_LENGTH);
    } else {
      LOGLN_WARN(F("Only KOR00 accepted in PENDING state"));
    }
  } else if (currentState == RACE_RUNNING) {
    LOG_INFO(F("Logging checkpoint "));
    if (checkpointNum < 10) LOG_INFO(F("0"));
    LOGLN_INFO(checkpointNum);
    
    addCheckpointPress(checkpointNum, false);
    validCheckpoint = true;
    
    if (checkpointNum == 99) {
      LOGLN_INFO(F("Finish checkpoint detected - switching to PENDING"));
      currentState = RACE_PENDING;
      playMelody(FINISH_MELODY, FINISH_MELODY_LENGTH);
    } else {
      playSuccessTone();
    }
  }
  
  if (validCheckpoint) {
    printPressTable();
  } else {
    playMelody(ERROR_MELODY, ERROR_MELODY_LENGTH);
  }
}

void processReadoutTrigger() {
  LOGLN_DEBUG(F("Processing readout trigger"));
  
  String serializedTable = serializePressTable();
  String dumpUrl = "https://kor.swarm.ostuda.net/dump.html?table=" + serializedTable;
  
  LOG_INFO(F("Generated dump URL:"));
  LOGLN_INFO(dumpUrl);

  playMelody(READOUT_START_MELODY, READOUT_START_MELODY_LENGTH);
  
  if (writeUrlToNfc(dumpUrl)) {
    LOGLN_INFO(F("Successfully wrote dump URL to NFC card"));
    playMelody(READOUT_END_MELODY, READOUT_END_MELODY_LENGTH);
  } else {
    LOGLN_WARN(F("Failed to write dump URL to NFC card"));
    playMelody(ERROR_MELODY, ERROR_MELODY_LENGTH);
  }
}

void clearPressTable() {
  pressCount = 0;
  memset(pressTable, 0, sizeof(pressTable));
}

void addCheckpointPress(uint8_t checkpoint, bool isStart) {
  if (pressCount < 100) {
    pressTable[pressCount].checkpoint = checkpoint;
    
    // Store relative timestamp (milliseconds since race start)
    if (raceStartTime > 0 && !isStart) {
      pressTable[pressCount].timestamp = millis() - raceStartTime;
    } else {
      pressTable[pressCount].timestamp = 0;  // Race hasn't started yet
    }
    
    pressCount++;
  }
}

void printPressTable() {
  LOGLN_INFO(F("=== Current Press Table ==="));
  LOG_INFO(F("State: "));
  LOGLN_INFO(currentState == RACE_PENDING ? F("PENDING") : F("RUNNING"));
  LOG_INFO(F("Race start: "));
  LOGLN_INFO(raceStartTime > 0 ? String(raceStartTime) : F("Not set"));
  LOG_INFO(F("Presses: "));
  LOGLN_INFO(pressCount);
  
  for (uint8_t i = 0; i < pressCount; i++) {
    LOG_INFO(F("  KOR"));
    if (pressTable[i].checkpoint < 10) LOG_INFO(F("0"));
    LOG_INFO(pressTable[i].checkpoint);
    LOG_INFO(F(" at +"));
    
    // Display time in seconds.milliseconds format for readability
    uint32_t ms = pressTable[i].timestamp;
    uint32_t seconds = ms / 1000;
    uint32_t remainingMs = ms % 1000;
    
    LOG_INFO(seconds);
    LOG_INFO(F("."));
    if (remainingMs < 100) LOG_INFO(F("0"));
    if (remainingMs < 10) LOG_INFO(F("0"));
    LOG_INFO(remainingMs);
    LOGLN_INFO(F("s"));
  }
}