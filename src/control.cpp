#include "control.h"

Control::Control(const IO &io, Flap &flap) : m_io(io), m_flap(flap) {}

void Control::begin() {
  m_timeout_timestamp = millis();
  apply_flap();
}

Control::FlapState Control::flap_state() const {
  if (m_door == Door::TOP) return FlapState::CLOSED;
  if (m_door == Door::DOWN) return FlapState::HALF;
  return m_timed_out ? FlapState::TIMEOUT : FlapState::FULL;
}

uint32_t Control::get_time_left_sec() const {
  if (flap_state() != FlapState::FULL) return 0;
  const uint32_t elapsed = millis() - m_timeout_timestamp;
  return elapsed >= timeout_ms ? 0 : (timeout_ms - elapsed + 999) / 1000;
}

void Control::update_switches(uint8_t raw, uint32_t now) {
  if (raw != m_raw) {
    m_raw = raw;
    m_changed_at = now;
  }
  if (raw == m_stable || now - m_changed_at < debounce_ms) return;
  const uint8_t pressed = raw & ~m_stable;
  m_stable = raw;
  if (!pressed) return;
  if (raw == (IO::UPPER | IO::LOWER)) {
    m_unexpected_io = true;
    return;
  }
  if (pressed == IO::UPPER) {
    if (m_door == Door::DOWN) m_unexpected_io = true;
    m_door = m_door == Door::HALF ? Door::TOP : Door::HALF;
  } else {
    if (m_door == Door::TOP) m_unexpected_io = true;
    m_door = m_door == Door::DOWN ? Door::HALF : Door::DOWN;
  }
  m_timed_out = false;
  if (m_door == Door::HALF) m_timeout_timestamp = now;
}

void Control::apply_flap() {
  switch (flap_state()) {
  case FlapState::CLOSED:
  case FlapState::TIMEOUT: m_flap.set_flap(20); break;
  case FlapState::HALF: m_flap.set_flap(75); break;
  case FlapState::FULL: m_flap.set_flap(100); break;
  }
  m_flap.write_flap_value();
}

void Control::iterate() {
  const uint32_t now = millis();
  update_switches(m_io.active_switches(), now);
  if (flap_state() == FlapState::FULL && now - m_timeout_timestamp >= timeout_ms) {
    m_timed_out = true;
  }
  apply_flap();
}
