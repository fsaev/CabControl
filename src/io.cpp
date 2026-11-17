#include "io.h"
#include <Arduino.h>

IO::IO() {
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
