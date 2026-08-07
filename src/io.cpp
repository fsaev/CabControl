#include "io.h"
#include <Arduino.h>

IO::IO() : m_level(Levels::OFF) {
  pinMode(m_pin_off, INPUT_PULLUP);
  pinMode(m_pin_half, INPUT_PULLUP);
  pinMode(m_pin_full, INPUT_PULLUP);
  pinMode(m_pin_aux, INPUT_PULLUP);
}

std::vector<bool> IO::get_inputs() const {
  std::vector<bool> inputs;
  inputs.push_back(digitalRead(m_pin_off) == HIGH);
  inputs.push_back(digitalRead(m_pin_half) == HIGH);
  inputs.push_back(digitalRead(m_pin_full) == HIGH);
  inputs.push_back(digitalRead(m_pin_aux) == HIGH);
  return inputs;
}

uint32_t IO::get_timeout_timestamp() const {
  return m_timeout_timestamp;
}

void IO::iterate() {
  if (digitalRead(m_pin_full) == HIGH) {
    if (m_level != Levels::FULL) {
      m_level = Levels::FULL;
      m_timeout_timestamp = millis();
    }
  } else if (digitalRead(m_pin_half) == HIGH) {
    if (m_level != Levels::HALF) {
      m_level = Levels::HALF;
      m_timeout_timestamp = millis();
    }
  } else if (digitalRead(m_pin_off) == HIGH) {
    if (m_level != Levels::OFF) {
      m_level = Levels::OFF;
      m_timeout_timestamp = millis();
    }
  }

  if (millis() - m_timeout_timestamp > timeout_ms) {
    m_level = Levels::TIMEOUT;
  }
}
