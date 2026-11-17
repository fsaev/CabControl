#ifndef IO_H
#define IO_H

#include <stdint.h>
#include <vector>
#include <Arduino.h>

class IO {
public:
  IO();
  std::vector<bool> get_inputs() const;

private:
  static constexpr uint8_t m_pin_off = 3;
  static constexpr uint8_t m_pin_half = 4;
  static constexpr uint8_t m_pin_full = 5;
  static constexpr uint8_t m_pin_aux = 6;
};

#endif
