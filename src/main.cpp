#include <Arduino.h>
// Explicitly include core components without networking
#include <HardwareSerial.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_PN532.h>

#include "melodies.h"
#include "logging.h"

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

// Checkpoint press structure
struct CheckpointPress {
  uint8_t checkpoint;
  uint32_t timestamp;  // Relative time in milliseconds since race start
};

// Global variables
RaceState currentState = RACE_PENDING;
CheckpointPress pressTable[100];  // Max 100 checkpoint presses
uint8_t pressCount = 0;
uint32_t lastNfcCheck = 0;
uint32_t raceStartTime = 0;  // Timestamp in milliseconds when KOR00 was scanned (race start)
const uint32_t NFC_CHECK_INTERVAL = 500; // Check NFC every 500ms

// Function declarations
bool readNfcCard();
bool parseNdefRecord(uint8_t* data, uint16_t dataLength);
void processCheckpoint(uint8_t checkpointNum);
void processReadoutTrigger();
void clearPressTable();
void addCheckpointPress(uint8_t checkpoint);
String serializePressTable();
bool writeUrlToNfc(String url);
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

bool readNfcCard() {
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;
  
  // Check for NTAG213/215/216
  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
    LOGLN_INFO(F("NFC card detected"));
    
    // Log the UID for debugging
    LOG_DEBUG(F("UID Length: "));
    LOG_DEBUG(uidLength, DEC);
    LOG_DEBUG(F(" bytes, UID: "));
    if (LOG_LEVEL <= LOG_LEVEL_DEBUG) {
      for (uint8_t i = 0; i < uidLength; i++) {
        if (uid[i] < 0x10) LOG_DEBUG(F("0"));
        LOG_DEBUG(uid[i], HEX);
        if (i < uidLength - 1) LOG_DEBUG(F(" "));
      }
    }
    LOGLN_DEBUG();
    
    // Read NDEF data from the card
    uint8_t data[144];
    
    // Try to read NDEF record from page 4 onwards (NTAG213 NDEF starts at page 4)
    bool success = false;
    
    // Read the full user memory area (pages 4-39 for NTAG213)
    uint16_t bytesRead = 0;
    for (uint8_t page = 4; page <= 39 && bytesRead < sizeof(data); page++) {
      if (nfc.ntag2xx_ReadPage(page, data + bytesRead)) {
        LOG_DEBUG(F("Read page "));
        LOG_DEBUG(page);
        LOG_DEBUG(F(": "));
        if (LOG_LEVEL <= LOG_LEVEL_DEBUG) {
          for (uint8_t i = 0; i < 4; i++) {
            if (data[bytesRead + i] < 0x10) LOG_DEBUG(F("0"));
            LOG_DEBUG(data[bytesRead + i], HEX);
            LOG_DEBUG(F(" "));
          }
        }
        LOGLN_DEBUG();
        bytesRead += 4;
      } else {
        LOG_DEBUG(F("Failed to read page "));
        LOGLN_DEBUG(page);
        break; // Stop if we can't read a page
      }
    }
    
    LOG_DEBUG(F("Total bytes read: "));
    LOGLN_DEBUG(bytesRead);
    
    if (bytesRead > 0) {
      if (LOG_LEVEL <= LOG_LEVEL_DEBUG) {
        Serial.println(F("Raw data hex dump:"));
        for (uint16_t i = 0; i < bytesRead; i++) {
          if (i % 16 == 0) {
            Serial.print(F("0x"));
            if (i < 0x100) Serial.print(F("0"));
            if (i < 0x10) Serial.print(F("0"));
            Serial.print(i, HEX);
            Serial.print(F(": "));
          }
          if (data[i] < 0x10) Serial.print(F("0"));
          Serial.print(data[i], HEX);
          Serial.print(F(" "));
          if ((i + 1) % 16 == 0 || i == bytesRead - 1) {
            // Print ASCII representation
            Serial.print(F(" |"));
            uint16_t lineStart = (i / 16) * 16;
            for (uint16_t j = lineStart; j <= i; j++) {
              char c = (char)data[j];
              if (c >= 32 && c <= 126) {
                Serial.print(c);
              } else {
                Serial.print(F("."));
              }
            }
            Serial.println(F("|"));
          }
        }
        Serial.println();
      }
      
      success = parseNdefRecord(data, bytesRead);
    }
    
    if (!success) {
      LOGLN_WARN(F("No valid KOR data found"));
      playMelody(ERROR_MELODY, ERROR_MELODY_LENGTH);
    }
    
    delay(5000); // Cooldown period before allowing next read
    return success;
  }
  
  return false;
}

bool parseNdefRecord(uint8_t* data, uint16_t dataLength) {
  // Look for NDEF record structure
  // Simple parser for text records and URL records
  
  LOG_DEBUG(F("Parsing NDEF record, data length: "));
  LOGLN_DEBUG(dataLength);
  
  for (uint16_t i = 0; i < dataLength - 6; i++) {
    // Check for text record with KOR prefix
    if (data[i] == 0x03 && i + 1 < dataLength) { // NDEF message TLV
      LOG_DEBUG(F("Found NDEF message TLV at position "));
      LOGLN_DEBUG(i);
      uint8_t recordLength = data[i + 1];
      LOG_DEBUG(F("NDEF message length: "));
      LOGLN_DEBUG(recordLength);
      
      if (recordLength >= 5 && i + 2 + recordLength <= dataLength) {
        // NDEF record starts at i + 2
        uint16_t recordStart = i + 2;

        if (LOG_LEVEL <= LOG_LEVEL_DEBUG) {
          Serial.print(F("Checking record at position "));
          Serial.print(recordStart);
          Serial.print(F(": TNF=0x"));
          if (recordStart < dataLength) {
            Serial.print(data[recordStart], HEX);
            Serial.print(F(", TypeLen=0x"));
            if (recordStart + 1 < dataLength) {
              Serial.print(data[recordStart + 1], HEX);
              Serial.print(F(", PayloadLen=0x"));
              if (recordStart + 2 < dataLength) {
                Serial.print(data[recordStart + 2], HEX);
                Serial.print(F(", Type="));
                if (recordStart + 3 < dataLength) {
                  Serial.print((char)data[recordStart + 3]);
                }
              }
            }
          }
          Serial.println();
        }
        
        // Look for TNF=1 (Well Known), Type=T (Text)
        if (recordStart + 3 < dataLength && 
            data[recordStart] == 0xD1 && 
            data[recordStart + 1] == 0x01 && 
            data[recordStart + 3] == 'T') {
          LOGLN_DEBUG(F("Found valid text record!"));
          // Parse text record
          uint8_t statusByte = data[recordStart + 4];
          uint8_t langLength = statusByte & 0x3F;  // Lower 6 bits are language code length
          uint8_t textStart = recordStart + 5 + langLength;
          
          LOG_DEBUG(F("Status byte: 0x"));
          LOG_DEBUG(statusByte, HEX);
          LOG_DEBUG(F(", Language length: "));
          LOG_DEBUG(langLength);
          LOG_DEBUG(F(", text starts at position: "));
          LOGLN_DEBUG(textStart);
          
          if (textStart + 5 <= dataLength) {
            if (LOG_LEVEL <= LOG_LEVEL_DEBUG) {
              Serial.print(F("Text content: "));
              for (uint8_t j = 0; j < 8 && textStart + j < dataLength; j++) {
                char c = (char)data[textStart + j];
                if (c >= 32 && c <= 126) {
                  Serial.print(c);
                } else {
                  Serial.print(F("[0x"));
                  Serial.print(data[textStart + j], HEX);
                  Serial.print(F("]"));
                }
              }
              Serial.println();
            }
            
            if (data[textStart] == 'K' && data[textStart + 1] == 'O' && data[textStart + 2] == 'R') {
              LOGLN_DEBUG(F("Found KOR prefix!"));
              
              // Extract checkpoint number
              if (textStart + 4 < dataLength) {
                char digit1 = data[textStart + 3];
                char digit2 = data[textStart + 4];
                LOG_DEBUG(F("Checkpoint digits: '"));
                LOG_DEBUG(digit1);
                LOG_DEBUG(F("' '"));
                LOG_DEBUG(digit2);
                LOGLN_DEBUG(F("'"));
                
                if (digit1 >= '0' && digit1 <= '9' && digit2 >= '0' && digit2 <= '9') {
                  uint8_t checkpoint = (digit1 - '0') * 10 + (digit2 - '0');
                  LOG_INFO(F("Found checkpoint: KOR")); 
                  if (checkpoint < 10) LOG_INFO(F("0"));
                  LOGLN_INFO(checkpoint);
                  
                  processCheckpoint(checkpoint);
                  return true;
                } else {
                  LOGLN_WARN(F("Invalid checkpoint digits"));
                }
              } else {
                LOGLN_WARN(F("Not enough data for checkpoint digits"));
              }
            } else {
              LOGLN_WARN(F("No KOR prefix found"));
            }
          } else {
            LOGLN_WARN(F("Text start position exceeds data length"));
          }
        }
        
        // Also check for URI record in this same NDEF message  
        else if (recordStart + 3 < dataLength && 
                 data[recordStart] == 0xD1 && 
                 data[recordStart + 1] == 0x01 && 
                 data[recordStart + 3] == 'U') {
          LOGLN_DEBUG(F("Found a valid URL record"));
          // Parse URI record
          uint8_t payloadLength = data[recordStart + 2];
          uint8_t uriStart = recordStart + 4;
          
          LOG_DEBUG(F("URI payload length: "));
          LOGLN_DEBUG(payloadLength);
          
          if (payloadLength > 0 && uriStart < dataLength) {
            // First byte is URI identifier code
            uint8_t uriCode = data[uriStart];
            LOG_DEBUG(F("URI identifier code: 0x"));
            LOGLN_DEBUG(uriCode, HEX);
            
            // Convert URI identifier code to prefix
            String prefix = "";
            switch (uriCode) {
              case 0x01: prefix = "http://www."; break;
              case 0x02: prefix = "https://www."; break;
              case 0x03: prefix = "http://"; break;
              case 0x04: prefix = "https://"; break;
              case 0x05: prefix = "tel:"; break;
              case 0x06: prefix = "mailto:"; break;
              default: prefix = ""; break; // 0x00 or unknown = no prefix
            }
            
            LOG_DEBUG(F("URI prefix: "));
            LOGLN_DEBUG(prefix);
            
            // Build complete URL from prefix + remaining payload
            String url = prefix;
            for (uint8_t j = 1; j < payloadLength && uriStart + j < dataLength; j++) {
              if (data[uriStart + j] == 0x00) break;
              url += (char)data[uriStart + j];
            }
            
            LOG_DEBUG(F("Complete URL: "));
            LOGLN_DEBUG(url);
            
            if (url.startsWith("https://kor.swarm.ostuda.net/")) {
              LOGLN_INFO(F("Found readout trigger"));
              processReadoutTrigger();
              return true;
            } else {
              LOGLN_WARN(F("URL doesn't match expected pattern"));
            }
          } else {
            LOGLN_WARN(F("URI payload is empty or invalid"));
          }
        }
      }
    }
  }
  
  LOGLN_WARN(F("No valid NDEF records found after parsing complete"));
  return false;
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
      addCheckpointPress(0);
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
    
    addCheckpointPress(checkpointNum);
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

void addCheckpointPress(uint8_t checkpoint) {
  if (pressCount < 100) {
    pressTable[pressCount].checkpoint = checkpoint;
    
    // Store relative timestamp (milliseconds since race start)
    if (raceStartTime > 0) {
      pressTable[pressCount].timestamp = millis() - raceStartTime;
    } else {
      pressTable[pressCount].timestamp = 0;  // Race hasn't started yet
    }
    
    pressCount++;
  }
}

// Helper function to encode binary data as base64url
String binaryToBase64Url(uint8_t* data, uint16_t length) {
  if (length == 0) return "A";
  
  // Base64URL characters: A-Z, a-z, 0-9, -, _
  const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  String result = "";
  
  // Process 3 bytes at a time (24 bits -> 4 base64 chars)
  uint16_t i = 0;
  while (i + 2 < length) {
    uint32_t block = (data[i] << 16) | (data[i+1] << 8) | data[i+2];
    
    result += chars[(block >> 18) & 0x3F];
    result += chars[(block >> 12) & 0x3F];
    result += chars[(block >> 6) & 0x3F];
    result += chars[block & 0x3F];
    
    i += 3;
  }
  
  // Handle remaining 1 or 2 bytes
  if (i < length) {
    uint32_t block = data[i] << 16;
    if (i + 1 < length) {
      block |= data[i+1] << 8;
    }
    
    result += chars[(block >> 18) & 0x3F];
    result += chars[(block >> 12) & 0x3F];
    
    if (i + 1 < length) {
      result += chars[(block >> 6) & 0x3F];
    }
  }
  
  return result;
}

String serializePressTable() {
  if (pressCount == 0) return "";
  
  // BINARY-PACKED ENCODING FORMAT:
  // Pack each checkpoint as: [1 byte checkpoint][3 bytes timestamp]
  // 3 bytes = 24 bits = 16.7M milliseconds = 4.6 hours (plenty for orienteering)
  // Then base64url-encode the entire binary blob (16% better than base36)
  // Uses A-Z, a-z, 0-9, -, _ for maximum URL-safe compression
  
  uint16_t binarySize = pressCount * 4; // 4 bytes per checkpoint
  uint8_t binaryData[binarySize];
  uint16_t offset = 0;
  
  for (uint8_t i = 0; i < pressCount; i++) {
    // Pack checkpoint number (1 byte)
    binaryData[offset++] = pressTable[i].checkpoint;
    
    // Pack timestamp (3 bytes, big-endian) - max 16.7M milliseconds
    uint32_t timestamp = pressTable[i].timestamp;
    if (timestamp > 0xFFFFFF) timestamp = 0xFFFFFF; // Clamp to 24-bit max
    binaryData[offset++] = (timestamp >> 16) & 0xFF;
    binaryData[offset++] = (timestamp >> 8) & 0xFF;
    binaryData[offset++] = timestamp & 0xFF;
  }
  
  // Convert binary data to base64url
  return binaryToBase64Url(binaryData, binarySize);
}

bool writeUrlToNfc(String url) {
  // Create NDEF URL record
  uint8_t ndef_data[144];  // Match read buffer size for consistency
  uint16_t ndef_length = 0;
  
  // NDEF Message header
  ndef_data[ndef_length++] = 0x03; // NDEF Message TLV
  
  // Determine URI payload format
  String urlSuffix = url;
  uint8_t uriCode = 0x00; // Default: no prefix
  
  if (url.startsWith("https://")) {
    uriCode = 0x04;
    urlSuffix = url.substring(8); // Remove "https://"
  } else if (url.startsWith("http://")) {
    uriCode = 0x03;
    urlSuffix = url.substring(7); // Remove "http://"
  }
  
  // Calculate the total record length first
  uint16_t payload_length = 1 + urlSuffix.length(); // URI code + URL suffix
  uint16_t record_length = 1 + 1 + 1 + 1 + payload_length; // TNF + TypeLen + PayloadLen + Type + Payload
  
  LOG_DEBUG(F("Writing URL: "));
  LOGLN_DEBUG(url);
  LOG_DEBUG(F("URI code: 0x"));
  LOG_DEBUG(uriCode, HEX);
  LOG_DEBUG(F(", suffix: "));
  LOGLN_DEBUG(urlSuffix);
  LOG_DEBUG(F("Record length: "));
  LOGLN_DEBUG(record_length);
  
  // Handle NDEF message length (can be 1 or 3 bytes)
  if (record_length <= 254) {
    ndef_data[ndef_length++] = record_length; // Single byte length
  } else {
    ndef_data[ndef_length++] = 0xFF; // Indicates 3-byte length follows
    ndef_data[ndef_length++] = (record_length >> 8) & 0xFF; // High byte
    ndef_data[ndef_length++] = record_length & 0xFF; // Low byte
  }
  
  // NDEF Record header  
  if (payload_length <= 255) {
    ndef_data[ndef_length++] = 0xD1; // TNF=1 (Well Known), MB=1, ME=1, SR=1 (short record)
    ndef_data[ndef_length++] = 0x01; // Type length = 1
    ndef_data[ndef_length++] = payload_length; // Payload length (single byte)
    ndef_data[ndef_length++] = 'U'; // Type = URI
  } else {
    ndef_data[ndef_length++] = 0xC1; // TNF=1 (Well Known), MB=1, ME=1, SR=0 (long record)
    ndef_data[ndef_length++] = 0x01; // Type length = 1
    ndef_data[ndef_length++] = (payload_length >> 24) & 0xFF; // Payload length (4 bytes)
    ndef_data[ndef_length++] = (payload_length >> 16) & 0xFF;
    ndef_data[ndef_length++] = (payload_length >> 8) & 0xFF;
    ndef_data[ndef_length++] = payload_length & 0xFF;
    ndef_data[ndef_length++] = 'U'; // Type = URI
  }
  
  // URI payload: identifier code + URL suffix
  ndef_data[ndef_length++] = uriCode;
  for (uint16_t i = 0; i < urlSuffix.length() && ndef_length < sizeof(ndef_data); i++) {
    ndef_data[ndef_length++] = urlSuffix[i];
  }
  
  // Add terminator TLV
  ndef_data[ndef_length++] = 0xFE;
  
  if (LOG_LEVEL <= LOG_LEVEL_DEBUG) {
    Serial.print(F("Total NDEF data length: "));
    Serial.println(ndef_length);
    Serial.println(F("NDEF data to write:"));
    for (uint16_t i = 0; i < ndef_length; i++) {
      if (i % 16 == 0) {
        Serial.print(F("0x"));
        if (i < 0x100) Serial.print(F("0"));
        if (i < 0x10) Serial.print(F("0"));
        Serial.print(i, HEX);
        Serial.print(F(": "));
      }
      if (ndef_data[i] < 0x10) Serial.print(F("0"));
      Serial.print(ndef_data[i], HEX);
      Serial.print(F(" "));
      if ((i + 1) % 16 == 0 || i == ndef_length - 1) {
        Serial.println();
      }
    }
  }
  
  // Write to NFC card starting at page 4
  uint8_t page = 4;
  uint16_t offset = 0;
  
  while (offset < ndef_length) {
    uint8_t page_data[4] = {0, 0, 0, 0};
    
    for (uint8_t i = 0; i < 4 && offset + i < ndef_length; i++) {
      page_data[i] = ndef_data[offset + i];
    }
    
    if (!nfc.ntag2xx_WritePage(page, page_data)) {
      LOG_WARN(F("Failed to write page "));
      LOGLN_WARN(page);
      return false;
    }
    
    LOG_DEBUG(F("Wrote page "));
    LOG_DEBUG(page);
    LOG_DEBUG(F(": "));
    for (uint8_t i = 0; i < 4; i++) {
      if (page_data[i] < 0x10) LOG_DEBUG(F("0"));
      LOG_DEBUG(page_data[i], HEX);
      LOG_DEBUG(F(" "));
    }
    LOGLN_DEBUG(F(""));
    
    page++;
    offset += 4;
    
    if (page > 39) break; // Don't exceed NTAG213 user memory (pages 4-39)
  }
  
  return true;
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