#ifndef NFC_H
#define NFC_H

#include <Arduino.h>

bool readNfcCard();
bool parseNdefRecord(uint8_t* data, uint16_t dataLength);
bool writeUrlToNfc(String url);
#endif