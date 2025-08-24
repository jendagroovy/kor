#include <Arduino.h>
// Explicitly include core components without networking
#include <HardwareSerial.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_PN532.h>

#include "melodies.h"

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
  Serial.println(F("KOR Orienteering Checkpoint Tracker"));
  
  // Initialize buzzer pin
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Play startup tone
  playMelody(INIT_MELODY, INIT_MELODY_LENGTH);
  
  // Initialize PN532
  nfc.begin();
  
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println(F("Didn't find PN532 board"));
    delay(500);
    playMelody(ERROR_MELODY, ERROR_MELODY_LENGTH);
    delay(500);
    playMelody(ERROR_MELODY, ERROR_MELODY_LENGTH);
    delay(500);
    playMelody(ERROR_MELODY, ERROR_MELODY_LENGTH);
    while (1) delay(1000); // halt
  }
  
  Serial.print(F("Found chip PN5")); Serial.println((versiondata>>24) & 0xFF, HEX);
  Serial.print(F("Firmware ver. ")); Serial.print((versiondata>>16) & 0xFF, DEC);
  Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);
  
  // Configure for reading NTAG213/215/216
  nfc.SAMConfig();
  
  Serial.println(F("System ready - PENDING state"));
  Serial.println(F("Present KOR00 to start tracking"));
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
    Serial.println(F("NFC card detected"));
    
    // Log the UID for debugging
    Serial.print(F("UID Length: "));
    Serial.print(uidLength, DEC);
    Serial.print(F(" bytes, UID: "));
    for (uint8_t i = 0; i < uidLength; i++) {
      if (uid[i] < 0x10) Serial.print(F("0"));
      Serial.print(uid[i], HEX);
      if (i < uidLength - 1) Serial.print(F(" "));
    }
    Serial.println();
    
    // Read NDEF data from the card
    uint8_t data[144];
    
    // Try to read NDEF record from page 4 onwards (NTAG213 NDEF starts at page 4)
    bool success = false;
    
    // Read the full user memory area (pages 4-39 for NTAG213)
    uint16_t bytesRead = 0;
    for (uint8_t page = 4; page <= 39 && bytesRead < sizeof(data); page++) {
      if (nfc.ntag2xx_ReadPage(page, data + bytesRead)) {
        Serial.print(F("Read page "));
        Serial.print(page);
        Serial.print(F(": "));
        for (uint8_t i = 0; i < 4; i++) {
          if (data[bytesRead + i] < 0x10) Serial.print(F("0"));
          Serial.print(data[bytesRead + i], HEX);
          Serial.print(F(" "));
        }
        Serial.println();
        bytesRead += 4;
      } else {
        Serial.print(F("Failed to read page "));
        Serial.println(page);
        break; // Stop if we can't read a page
      }
    }
    
    Serial.print(F("Total bytes read: "));
    Serial.println(bytesRead);
    
    if (bytesRead > 0) {
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
      
      success = parseNdefRecord(data, bytesRead);
    }
    
    if (!success) {
      Serial.println(F("No valid KOR data found"));
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
  
  Serial.print(F("Parsing NDEF record, data length: "));
  Serial.println(dataLength);
  
  for (uint16_t i = 0; i < dataLength - 6; i++) {
    // Check for text record with KOR prefix
    if (data[i] == 0x03 && i + 1 < dataLength) { // NDEF message TLV
      Serial.print(F("Found NDEF message TLV at position "));
      Serial.println(i);
      uint8_t recordLength = data[i + 1];
      Serial.print(F("NDEF message length: "));
      Serial.println(recordLength);
      
      if (recordLength >= 5 && i + 2 + recordLength <= dataLength) {
        // NDEF record starts at i + 2
        uint16_t recordStart = i + 2;
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
        
        // Look for TNF=1 (Well Known), Type=T (Text)
        if (recordStart + 3 < dataLength && 
            data[recordStart] == 0xD1 && 
            data[recordStart + 1] == 0x01 && 
            data[recordStart + 3] == 'T') {
          Serial.println(F("Found valid text record!"));
          // Parse text record
          uint8_t statusByte = data[recordStart + 4];
          uint8_t langLength = statusByte & 0x3F;  // Lower 6 bits are language code length
          uint8_t textStart = recordStart + 5 + langLength;
          
          Serial.print(F("Status byte: 0x"));
          Serial.print(statusByte, HEX);
          Serial.print(F(", Language length: "));
          Serial.print(langLength);
          Serial.print(F(", text starts at position: "));
          Serial.println(textStart);
          
          if (textStart + 5 <= dataLength) {
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
            
            if (data[textStart] == 'K' && data[textStart + 1] == 'O' && data[textStart + 2] == 'R') {
              Serial.println(F("Found KOR prefix!"));
              
              // Extract checkpoint number
              if (textStart + 4 < dataLength) {
                char digit1 = data[textStart + 3];
                char digit2 = data[textStart + 4];
                Serial.print(F("Checkpoint digits: '"));
                Serial.print(digit1);
                Serial.print(F("' '"));
                Serial.print(digit2);
                Serial.println(F("'"));
                
                if (digit1 >= '0' && digit1 <= '9' && digit2 >= '0' && digit2 <= '9') {
                  uint8_t checkpoint = (digit1 - '0') * 10 + (digit2 - '0');
                  Serial.print(F("Found checkpoint: KOR")); 
                  if (checkpoint < 10) Serial.print(F("0"));
                  Serial.println(checkpoint);
                  
                  processCheckpoint(checkpoint);
                  return true;
                } else {
                  Serial.println(F("Invalid checkpoint digits"));
                }
              } else {
                Serial.println(F("Not enough data for checkpoint digits"));
              }
            } else {
              Serial.println(F("No KOR prefix found"));
            }
          } else {
            Serial.println(F("Text start position exceeds data length"));
          }
        }
        
        // Also check for URI record in this same NDEF message  
        else if (recordStart + 3 < dataLength && 
                 data[recordStart] == 0xD1 && 
                 data[recordStart + 1] == 0x01 && 
                 data[recordStart + 3] == 'U') {
          Serial.println(F("Found valid URI record!"));
          // Parse URI record
          uint8_t payloadLength = data[recordStart + 2];
          uint8_t uriStart = recordStart + 4;
          
          Serial.print(F("URI payload length: "));
          Serial.println(payloadLength);
          
          if (payloadLength > 0 && uriStart < dataLength) {
            // First byte is URI identifier code
            uint8_t uriCode = data[uriStart];
            Serial.print(F("URI identifier code: 0x"));
            Serial.println(uriCode, HEX);
            
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
            
            Serial.print(F("URI prefix: "));
            Serial.println(prefix);
            
            // Build complete URL from prefix + remaining payload
            String url = prefix;
            for (uint8_t j = 1; j < payloadLength && uriStart + j < dataLength; j++) {
              if (data[uriStart + j] == 0x00) break;
              url += (char)data[uriStart + j];
            }
            
            Serial.print(F("Complete URL: "));
            Serial.println(url);
            
            if (url.startsWith("https://kor.swarm.ostuda.net/")) {
              Serial.println(F("Found readout trigger"));
              processReadoutTrigger();
              return true;
            } else {
              Serial.println(F("URL doesn't match expected pattern"));
            }
          } else {
            Serial.println(F("URI payload is empty or invalid"));
          }
        }
      }
    }
  }
  
  Serial.println(F("No valid NDEF records found after parsing complete"));
  return false;
}

void processCheckpoint(uint8_t checkpointNum) {
  bool validCheckpoint = false;
  
  if (currentState == RACE_PENDING) {
    if (checkpointNum == 0) {
      Serial.println(F("Start checkpoint detected - clearing table and switching to RUNNING"));
      clearPressTable();  // Clear everything first
      raceStartTime = millis();  // Set race start time baseline in milliseconds
      Serial.print(F("Race start time set to: "));
      Serial.println(raceStartTime);
      addCheckpointPress(0);
      currentState = RACE_RUNNING;
      validCheckpoint = true;
      playMelody(INIT_MELODY, INIT_MELODY_LENGTH);
    } else {
      Serial.println(F("Only KOR00 accepted in PENDING state"));
    }
  } else if (currentState == RACE_RUNNING) {
    Serial.print(F("Logging checkpoint "));
    if (checkpointNum < 10) Serial.print(F("0"));
    Serial.println(checkpointNum);
    
    addCheckpointPress(checkpointNum);
    validCheckpoint = true;
    
    if (checkpointNum == 99) {
      Serial.println(F("Finish checkpoint detected - switching to PENDING"));
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
  Serial.println(F("Processing readout trigger"));
  
  String serializedTable = serializePressTable();
  String dumpUrl = "https://kor.swarm.ostuda.net/dump.html?table=" + serializedTable;
  
  Serial.println(F("Generated dump URL:"));
  Serial.println(dumpUrl);

  playMelody(READOUT_START_MELODY, READOUT_START_MELODY_LENGTH);
  
  if (writeUrlToNfc(dumpUrl)) {
    Serial.println(F("Successfully wrote dump URL to NFC card"));
    playMelody(READOUT_END_MELODY, READOUT_END_MELODY_LENGTH);
  } else {
    Serial.println(F("Failed to write dump URL to NFC card"));
    playMelody(ERROR_MELODY, ERROR_MELODY_LENGTH);
  }
}

void clearPressTable() {
  pressCount = 0;
  memset(pressTable, 0, sizeof(pressTable));
  Serial.println(F("Press table cleared"));
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
  
  Serial.print(F("Writing URL: "));
  Serial.println(url);
  Serial.print(F("URI code: 0x"));
  Serial.print(uriCode, HEX);
  Serial.print(F(", suffix: "));
  Serial.println(urlSuffix);
  Serial.print(F("Record length: "));
  Serial.println(record_length);
  
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
  
  // Write to NFC card starting at page 4
  uint8_t page = 4;
  uint16_t offset = 0;
  
  while (offset < ndef_length) {
    uint8_t page_data[4] = {0, 0, 0, 0};
    
    for (uint8_t i = 0; i < 4 && offset + i < ndef_length; i++) {
      page_data[i] = ndef_data[offset + i];
    }
    
    if (!nfc.ntag2xx_WritePage(page, page_data)) {
      Serial.print(F("Failed to write page "));
      Serial.println(page);
      return false;
    }
    
    Serial.print(F("Wrote page "));
    Serial.print(page);
    Serial.print(F(": "));
    for (uint8_t i = 0; i < 4; i++) {
      if (page_data[i] < 0x10) Serial.print(F("0"));
      Serial.print(page_data[i], HEX);
      Serial.print(F(" "));
    }
    Serial.println();
    
    page++;
    offset += 4;
    
    if (page > 39) break; // Don't exceed NTAG213 user memory (pages 4-39)
  }
  
  return true;
}

void printPressTable() {
  Serial.println(F("=== Current Press Table ==="));
  Serial.print(F("State: "));
  Serial.println(currentState == RACE_PENDING ? F("PENDING") : F("RUNNING"));
  Serial.print(F("Race start: "));
  Serial.println(raceStartTime > 0 ? String(raceStartTime) : F("Not set"));
  Serial.print(F("Presses: "));
  Serial.println(pressCount);
  
  for (uint8_t i = 0; i < pressCount; i++) {
    Serial.print(F("  KOR"));
    if (pressTable[i].checkpoint < 10) Serial.print(F("0"));
    Serial.print(pressTable[i].checkpoint);
    Serial.print(F(" at +"));
    
    // Display time in seconds.milliseconds format for readability
    uint32_t ms = pressTable[i].timestamp;
    uint32_t seconds = ms / 1000;
    uint32_t remainingMs = ms % 1000;
    
    Serial.print(seconds);
    Serial.print(F("."));
    if (remainingMs < 100) Serial.print(F("0"));
    if (remainingMs < 10) Serial.print(F("0"));
    Serial.print(remainingMs);
    Serial.println(F("s"));
  }
  
  String compactData = serializePressTable();
  Serial.println(F("Base64URL-packed data: "));
  Serial.print(compactData);
  Serial.print(F(" ("));
  Serial.print(compactData.length());
  Serial.print(F(" chars, "));
  Serial.print(pressCount * 4);
  Serial.println(F(" bytes packed)"));
  Serial.println(F("=========================="));
}