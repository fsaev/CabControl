#include "io.h"
#include <Arduino.h>

IO::IO() {
  pinMode(upper_pin, INPUT_PULLUP);
  pinMode(lower_pin, INPUT_PULLUP);
}

uint8_t IO::active_switches() const {
  return (digitalRead(upper_pin) == LOW ? UPPER : 0) |
         (digitalRead(lower_pin) == LOW ? LOWER : 0);
}
