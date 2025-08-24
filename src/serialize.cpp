#include <Arduino.h>

#include "serialize.h"
#include "main.h"


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