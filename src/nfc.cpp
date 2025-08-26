#include <Arduino.h>
#include <Adafruit_PN532.h>
#include "logging.h"
#include "melodies.h"
#include "main.h"

#include "nfc.h"

extern Adafruit_PN532 nfc;


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
    } else {
      delay(5000); // Cooldown period before allowing next read
    }

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
                    uint8_t courseLen = 0;

                    // Check if this is KOR00 and if there's a course length specified
                    if (checkpoint == 0 && textStart + 6 < dataLength &&
                        data[textStart + 5] == '/' &&
                        data[textStart + 6] >= '0' && data[textStart + 6] <= '9') {

                        courseLen = data[textStart + 6] - '0';

                        // Check for second digit
                        if (textStart + 7 < dataLength &&
                            data[textStart + 7] >= '0' && data[textStart + 7] <= '9') {
                            courseLen = courseLen * 10 + (data[textStart + 7] - '0');
                        }
                    }

                    LOG_INFO(F("Found checkpoint: KOR"));
                    if (checkpoint < 10) LOG_INFO(F("0"));
                    LOGLN_INFO(checkpoint);
                    if (courseLen > 0) {
                      LOG_INFO(F("Course length configured to: "));
                      LOGLN_INFO(courseLen);
                    }

                    processCheckpoint(checkpoint, courseLen);
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
