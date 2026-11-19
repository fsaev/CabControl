#ifndef OLED_H
#define OLED_H

#include "bluetooth.h"
#include "flap.h"
#include "io.h"
#include "power.h"
#include <Adafruit_SSD1306.h>
#include <stdint.h>

class Control;

class Oled {
public:
  Oled(Power& power, Flap& flap, Bluetooth& bluetooth);
  void init();
  void iterate(Control &control, const IO &io);

  bool is_initialized() const { return m_initialized; }
  bool power_error_ack_pending() const { return m_power_error_ack_required; }
  Power::Error latched_power_error() const { return m_latched_power_error; }

private:
  void iterate_sw(Control &control);
  static constexpr int m_screen_width = 96;
  static constexpr int m_screen_height = 16;
  static constexpr int m_oled_reset_pin = -1;
  static constexpr uint8_t m_screen_address = 0x3C;
  static constexpr uint16_t m_dim_timeout_ms = 5000;
  static constexpr uint32_t m_off_timeout_ms = 120000;
  static constexpr uint16_t m_page_interval_ms = 2000;
  static constexpr uint16_t m_error_blink_interval_ms = 500;
  static constexpr uint8_t m_max_pages = 5;
  static constexpr uint8_t m_line_length = 16;

  static constexpr uint8_t SW_LEFT_PIN = 9;
  static constexpr uint8_t SW_ENTER_PIN = 10;
  static constexpr uint8_t SW_RIGHT_PIN = 11;
  static constexpr uint16_t m_sw_poll_interval_ms = 100;
  static constexpr uint16_t m_oled_refresh_interval_ms = 100;

  static constexpr uint32_t WELCOME_TIMEOUT_MS = 2000;

  enum class MENU_STATE {
    STATE_START = 0,
    FLAP_STATUS = 1,
    OUT_MONITOR = 2,
    INPUT_MONITOR = 3,
    TIMEOUT_MONITOR_MIN = 4,
    TIMEOUT_MONITOR_SEC = 5,
    ABOUT = 6,
    BLUETOOTH = 7,
    IDENTITY = 8,
    POWER_ERROR = 9,
    IO_ERROR = 10,
    STATE_END = 11
  };

  Power& m_power;
  Flap& m_flap;
  Bluetooth& m_bluetooth;

  bool m_welcome_display = true;
  MENU_STATE m_menu_state = MENU_STATE::FLAP_STATUS;
  MENU_STATE m_last_menu_state = MENU_STATE::STATE_START;
  Power::Error m_last_power_error = Power::Error::NONE;
  Power::Error m_latched_power_error = Power::Error::NONE;
  bool m_power_error_ack_required = false;
  static constexpr auto m_menu_size =
      static_cast<std::size_t>(MENU_STATE::STATE_END);
  uint32_t m_menu_timestamp = 0;
  uint32_t m_page_timestamp = 0;
  uint32_t m_oled_refresh_timestamp = 0;

  bool is_present_on_bus() const;

  Adafruit_SSD1306 m_display;
  bool m_initialized;
  bool m_dimmed;
  bool m_display_off = false;
  uint32_t m_dim_timestamp;
  uint32_t m_sw_poll_timestamp;

  void reset_dimmed() {
    if (m_display_off) {
      m_display.ssd1306_command(SSD1306_DISPLAYON);
      m_display_off = false;
    }
    m_dimmed = false;
    m_dim_timestamp = millis();
  }
};

#endif
