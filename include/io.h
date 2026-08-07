#ifndef IO_H
#define IO_H

#include <stdint.h>
#include <vector>
#include <Arduino.h>

class IO {
public:
  enum class Levels { UNKNOWN, TIMEOUT, OFF, HALF, FULL };

  IO();
  std::vector<bool> get_inputs() const;
  uint32_t get_timeout_timestamp() const;
  uint32_t get_time_left_sec() const { return (m_timeout_timestamp + timeout_ms - millis()) / 1000; }
  uint32_t get_time_left_mins() const { return get_time_left_sec() / 60; }
  void iterate();
  Levels get_level() const { return m_level; }

private:
  Levels m_level = Levels::UNKNOWN;

  static constexpr uint8_t m_pin_off = 3;
  static constexpr uint8_t m_pin_half = 4;
  static constexpr uint8_t m_pin_full = 5;
  static constexpr uint8_t m_pin_aux = 6;

  static constexpr uint32_t timeout_ms = 3 * 1000 * 60 * 60; // 3 hours
  uint32_t m_timeout_timestamp = 0;
  bool m_timeout_active = false;
};

#endif
