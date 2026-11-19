#ifndef CONTROL_H
#define CONTROL_H

#include "flap.h"
#include "io.h"
#include <Arduino.h>
#include <stdint.h>

class Control {
public:
  enum class Door : uint8_t { TOP, HALF, DOWN };
  enum class FlapState : uint8_t { CLOSED, HALF, FULL, TIMEOUT };

  Control(const IO &io, Flap &flap);
  void iterate();
  void begin();
  Door door() const { return m_door; }
  FlapState flap_state() const;
  bool unexpected_io() const { return m_unexpected_io; }
  void acknowledge_io() { m_unexpected_io = false; }
  uint32_t get_time_left_sec() const;
  uint32_t get_time_left_mins() const { return get_time_left_sec() / 60; }

private:
  const IO &m_io;
  Flap &m_flap;
  Door m_door = Door::HALF;
  bool m_timed_out = false;
  bool m_unexpected_io = false;
  uint8_t m_raw = 0, m_stable = 0;
  uint32_t m_changed_at = 0;
  static constexpr uint32_t debounce_ms = 100;
  void update_switches(uint8_t raw, uint32_t now);
  void apply_flap();
  static constexpr uint32_t timeout_ms = 1 * 1000 * 60 * 60; // 1 hour
  uint32_t m_timeout_timestamp = 0;
};

#endif
