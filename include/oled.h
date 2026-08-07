#ifndef OLED_H
#define OLED_H

#include "flap.h"
#include "io.h"
#include <Adafruit_SSD1306.h>
#include <stdint.h>

class Oled {
public:
  Oled();
  void init();
  void iterate_sw();
  float get_dc_in_voltage() const;
  float get_dc_adj_voltage() const;
  void iterate(const Flap &flap, const IO &io);

private:
  static constexpr int m_screen_width = 96;
  static constexpr int m_screen_height = 16;
  static constexpr int m_oled_reset_pin = -1;
  static constexpr uint8_t m_screen_address = 0x3C;
  static constexpr uint16_t m_dim_timeout_ms = 5000;
  static constexpr uint16_t m_page_interval_ms = 2000;
  static constexpr uint8_t m_max_pages = 4;
  static constexpr uint8_t m_line_length = 16;

  static constexpr uint8_t SW_LEFT_PIN = 9;
  static constexpr uint8_t SW_ENTER_PIN = 10;
  static constexpr uint8_t SW_RIGHT_PIN = 11;
  static constexpr uint16_t m_sw_poll_interval_ms = 100;

  static constexpr uint32_t WELCOME_TIMEOUT_MS = 2000;

  enum class MENU_STATE {
    STATE_START = 0,
    FLAP_STATUS = 1,
    OUT_MONITOR = 2,
    INPUT_MONITOR = 3,
    TIMEOUT_MONITOR_MIN = 4,
    TIMEOUT_MONITOR_SEC = 5,
    ABOUT = 6,
    STATE_END = 7
  };

  bool m_welcome_display = true;
  MENU_STATE m_menu_state = MENU_STATE::FLAP_STATUS;
  MENU_STATE m_last_menu_state = MENU_STATE::STATE_START;
  static constexpr auto m_menu_size =
      static_cast<std::size_t>(MENU_STATE::STATE_END);
  uint32_t m_menu_timestamp = 0;
  uint32_t m_page_timestamp = 0;

  bool is_present_on_bus() const;

  Adafruit_SSD1306 m_display;
  bool m_initialized;
  bool m_dimmed;
  uint32_t m_dim_timestamp;
  uint32_t m_sw_poll_timestamp;

  void reset_dimmed() {
    m_dimmed = false;
    m_dim_timestamp = millis();
  }
};

#endif
