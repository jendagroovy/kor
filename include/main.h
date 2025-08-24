#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>

// Checkpoint press structure
struct CheckpointPress {
  uint8_t checkpoint;
  uint32_t timestamp;  // Relative time in milliseconds since race start
};

// Global variables
extern CheckpointPress pressTable[100];
extern uint8_t pressCount;

// Function declarations
void processReadoutTrigger();
void processCheckpoint(uint8_t checkpointNum, uint8_t courseLen);

#endif