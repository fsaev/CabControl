#ifndef CONTROL_H
#define CONTROL_H

#include "flap.h"
#include "io.h"
#include <Arduino.h>
#include <stdint.h>

class Control {
public:
  enum class Levels { UNKNOWN, TIMEOUT, OFF, HALF, FULL };

  Control(const IO &io, Flap &flap);
  void iterate();
  Levels get_level() const { return m_level; }
  // Level that timed out; UNKNOWN until the first timeout.
  Levels get_timeout_level() const { return m_timeout_level; }
  uint32_t get_timeout_timestamp() const { return m_timeout_timestamp; }
  uint32_t get_time_left_sec() const;
  uint32_t get_time_left_mins() const { return get_time_left_sec() / 60; }

private:
  const IO &m_io;
  Flap &m_flap;
  Levels m_level = Levels::OFF;
  Levels m_timeout_level = Levels::UNKNOWN;
  static constexpr uint32_t timeout_ms = 1 * 1000 * 60 * 60; // 1 hour
  uint32_t m_timeout_timestamp = 0;
};

#endif
