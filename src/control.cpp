#include "control.h"

Control::Control(const IO &io, Flap &flap) : m_io(io), m_flap(flap) {}

uint32_t Control::get_time_left_sec() const {
  if (m_level != Levels::HALF && m_level != Levels::FULL) {
    return 0;
  }
  const uint32_t elapsed = millis() - m_timeout_timestamp;
  return elapsed >= timeout_ms ? 0 : (timeout_ms - elapsed + 999) / 1000;
}

void Control::iterate() {
  const std::vector<bool> inputs = m_io.get_inputs();
  Levels selected_level = m_level == Levels::TIMEOUT ? m_timeout_level : m_level;
  if (!inputs[2]) {
    selected_level = Levels::FULL;
  } else if (!inputs[1]) {
    selected_level = Levels::HALF;
  } else if (!inputs[0]) {
    selected_level = Levels::OFF;
  }

  if (selected_level != m_level &&
      !(m_level == Levels::TIMEOUT && selected_level == m_timeout_level)) {
    m_level = selected_level;
    if (m_level == Levels::HALF || m_level == Levels::FULL) {
      m_timeout_timestamp = millis();
    }
  }

  if ((m_level == Levels::HALF || m_level == Levels::FULL) &&
      millis() - m_timeout_timestamp >= timeout_ms) {
    m_timeout_level = m_level;
    m_level = Levels::TIMEOUT;
  }

  switch (m_level) {
  case Levels::OFF:
    m_flap.set_flap(20);
    break;
  case Levels::HALF:
    m_flap.set_flap(75);
    break;
  case Levels::FULL:
    m_flap.set_flap(100);
    break;
  case Levels::TIMEOUT:
    m_flap.set_flap(20);
    break;
  case Levels::UNKNOWN:
    m_flap.set_flap(0); // 0 indicates error
    break;
  }

  m_flap.write_flap_value();
}
