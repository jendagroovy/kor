// Melodies for the KOR Orienteering Checkpoint Tracker
#ifndef MELODIES_H
#define MELODIES_H

#include "pitches.h"

// Pin definitions
#define BUZZER_PIN (15)  // D8

// Structure to hold note and duration pairs
struct Note {
  int frequency;
  int duration;
};

// Define melodies as constant arrays
const Note INIT_MELODY[] = {
  {NOTE_C4, 100},
  {NOTE_D4, 100},
  {NOTE_E4, 100},
  {NOTE_F4, 100},
  {NOTE_G4, 100}
};
const int INIT_MELODY_LENGTH = sizeof(INIT_MELODY) / sizeof(INIT_MELODY[0]);

const Note FINISH_MELODY[] = {
  {NOTE_C4, 90},
  {REST, 10},
  {NOTE_C4, 90},
  {REST, 10},
  {NOTE_C4, 90},
  {REST, 10},
  {NOTE_G4, 100},
  {REST, 100},
  {NOTE_C4, 100},
  {NOTE_G4, 400},
};
const int FINISH_MELODY_LENGTH = sizeof(FINISH_MELODY) / sizeof(FINISH_MELODY[0]);

const Note ERROR_MELODY[] = {
  {NOTE_C2, 1000},
};
const int ERROR_MELODY_LENGTH = sizeof(ERROR_MELODY) / sizeof(ERROR_MELODY[0]);

const Note MISS_MELODY[] = {
  {NOTE_CS4, 100},
  {NOTE_C4, 100},
  {NOTE_CS4, 100},
  {NOTE_C4, 100},
  {NOTE_CS4, 100},
  {NOTE_C4, 100},
  {NOTE_CS4, 100},
  {NOTE_C4, 100},
};
const int MISS_MELODY_LENGTH = sizeof(MISS_MELODY) / sizeof(MISS_MELODY[0]);

const Note READOUT_START_MELODY[] = {
  {NOTE_C4, 100},
  {NOTE_D4, 100},
  {NOTE_E4, 100},
};
const int READOUT_START_MELODY_LENGTH = sizeof(READOUT_START_MELODY) / sizeof(READOUT_START_MELODY[0]);

const Note READOUT_END_MELODY[] = {
  {NOTE_A4, 100},
  {NOTE_B4, 100},
  {NOTE_C5, 100},
};
const int READOUT_END_MELODY_LENGTH = sizeof(READOUT_END_MELODY) / sizeof(READOUT_END_MELODY[0]);

void playMelody(const Note melody[], int length);
void playBuzzer(int duration_ms);
void playSuccessTone();
void playLament();

#endif