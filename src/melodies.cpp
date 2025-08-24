#include <Arduino.h>
#include "melodies.h"

void playMelody(const Note melody[], int length) {
  for (int i = 0; i < length; i++) {
    if (melody[i].frequency == REST) {
      delay(melody[i].duration);  // Just pause for REST notes
    } else {
      tone(BUZZER_PIN, melody[i].frequency, melody[i].duration);
      delay(melody[i].duration);
    }
  }
}

void playBuzzer(int duration_ms) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(duration_ms);
  digitalWrite(BUZZER_PIN, LOW);
}

void playSuccessTone() {
  tone(BUZZER_PIN, 1500, 300);
  delay(300); // Wait for tone to complete
}