#ifndef IO_H
#define IO_H

#include <stdint.h>
#include <Arduino.h>

class IO {
public:
  IO();
  enum Switch : uint8_t { UPPER = 1, LOWER = 2 };
  uint8_t active_switches() const;

private:
  static constexpr uint8_t upper_pin = 3;
  static constexpr uint8_t lower_pin = 4;
};

#endif
