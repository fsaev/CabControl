/**************************************************************************
 This is an example for our Monochrome OLEDs based on SSD1306 drivers
 ------> http://www.adafruit.com/category/63_98

 This example is for a 128x32 pixel display using I2C to communicate
 3 pins are required to interface (two I2C and one reset).

 Adafruit invests time and resources providing this open
 source code, please support Adafruit and open-source
 hardware by purchasing products from Adafruit!

 Written by Limor Fried/Ladyada for Adafruit Industries,
 with contributions from the open source community.
 BSD license, check license.txt for more information
 All text above, and the splash screen below must be
 included in any redistribution.
 **************************************************************************/

#include "oled.h"
#include "control.h"
#include "params.h"

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

Oled::Oled(Power& power, Flap& flap, Bluetooth& bluetooth)
    : m_power(power),
      m_flap(flap),
      m_bluetooth(bluetooth),
      m_display(m_screen_width, m_screen_height, &Wire, m_oled_reset_pin),
      m_initialized(false), m_dimmed(false), m_dim_timestamp(0) {}

void Oled::init() {
  Wire.begin();
  if (!is_present_on_bus()) {
    m_initialized = false;
    return;
  }

  m_initialized = m_display.begin(SSD1306_SWITCHCAPVCC, m_screen_address);
  m_dim_timestamp = millis();
  m_menu_timestamp = millis();
  m_page_timestamp = millis();
  m_last_menu_state = m_menu_state;

  pinMode(SW_LEFT_PIN, INPUT_PULLUP);
  pinMode(SW_ENTER_PIN, INPUT_PULLUP);
  pinMode(SW_RIGHT_PIN, INPUT_PULLUP);
}

void Oled::iterate_sw(Control &control) {

  m_sw_poll_timestamp = millis();

  static bool sw_left_prev = HIGH, sw_enter_prev = HIGH, sw_right_prev = HIGH;

  bool sw_left = digitalRead(SW_LEFT_PIN);
  bool sw_enter = digitalRead(SW_ENTER_PIN);
  bool sw_right = digitalRead(SW_RIGHT_PIN);

  const bool left_pressed = sw_left_prev == HIGH && sw_left == LOW;
  const bool enter_pressed = sw_enter_prev == HIGH && sw_enter == LOW;
  const bool right_pressed = sw_right_prev == HIGH && sw_right == LOW;
  sw_left_prev = sw_left;
  sw_enter_prev = sw_enter;
  sw_right_prev = sw_right;
  if (!left_pressed && !enter_pressed && !right_pressed) {
    return;
  }
  const bool wake_only = m_display_off;
  reset_dimmed();
  if (wake_only) {
    return;
  }
  if (m_power_error_ack_required) {
    if (enter_pressed) {
      m_power_error_ack_required = false;
      m_page_timestamp = millis();
    }
  } else if (m_menu_state == MENU_STATE::IO_ERROR && control.unexpected_io()) {
    if (enter_pressed) control.acknowledge_io();
  } else if (left_pressed) {
    if (m_menu_state == static_cast<MENU_STATE>(static_cast<int>(MENU_STATE::STATE_START) + 1)) {
      m_menu_state =
          static_cast<MENU_STATE>(static_cast<int>(MENU_STATE::STATE_END) - 1);
    } else {
      m_menu_state =
          static_cast<MENU_STATE>(static_cast<int>(m_menu_state) - 1);
    }
  } else if (enter_pressed) {
    if (m_menu_state == MENU_STATE::BLUETOOTH && m_bluetooth.start_failed()) {
      m_bluetooth.retry();
      m_page_timestamp = millis();
    }
  } else if (right_pressed) {
    if (m_menu_state ==
        static_cast<MENU_STATE>(static_cast<int>(MENU_STATE::STATE_END) - 1)) {
      m_menu_state = static_cast<MENU_STATE>(
          static_cast<int>(MENU_STATE::STATE_START) + 1);
    } else {
      m_menu_state =
          static_cast<MENU_STATE>(static_cast<int>(m_menu_state) + 1);
    }
  }

  if (m_menu_state != m_last_menu_state) {
    m_page_timestamp = millis();
    m_last_menu_state = m_menu_state;
  }

}



void Oled::iterate(Control &control, const IO &io) {
  if (!m_initialized) {
    return;
  }

  if (millis() - m_oled_refresh_timestamp < m_oled_refresh_interval_ms) {
    return;
  }
  m_oled_refresh_timestamp = millis();

  if (m_menu_state != MENU_STATE::STATE_START) {
    iterate_sw(control);
  }
  const Power::Error power_error = m_power.get_error();
  if (power_error != m_last_power_error) {
    m_last_power_error = power_error;
    if (power_error != Power::Error::NONE) {
      m_latched_power_error = power_error;
      m_power_error_ack_required = true;
      m_menu_state = MENU_STATE::POWER_ERROR;
      m_last_menu_state = m_menu_state;
      m_welcome_display = false;
      m_page_timestamp = millis();
      reset_dimmed();
    }
  }

  if (control.unexpected_io() && !m_power_error_ack_required &&
      m_menu_state != MENU_STATE::IO_ERROR) {
    m_menu_state = m_last_menu_state = MENU_STATE::IO_ERROR;
    m_welcome_display = false;
    reset_dimmed();
  }

  const uint32_t inactive_ms = millis() - m_dim_timestamp;
  if (!m_display_off && inactive_ms >= m_off_timeout_ms) {
    m_display.ssd1306_command(SSD1306_DISPLAYOFF);
    m_display_off = true;
  }
  if (m_display_off) {
    return;
  }

  char top_line[m_max_pages][m_line_length] = {};
  char bottom_line[m_max_pages][m_line_length] = {};
  uint8_t pages = 1;

  if (m_welcome_display) {
    snprintf(top_line[0], sizeof(top_line[0]), "%s", m_bluetooth.name());
    snprintf(bottom_line[0], sizeof(bottom_line[0]), "Ver: %d.%d.%d",
             PARAMS_VERSION_MAJOR, PARAMS_VERSION_MINOR, PARAMS_VERSION_PATCH);
    if (millis() - m_menu_timestamp > WELCOME_TIMEOUT_MS) {
      m_welcome_display = false;
      m_page_timestamp = millis();
    }
  } else {
    switch (m_menu_state) {
    case MENU_STATE::FLAP_STATUS:
      snprintf(top_line[0], sizeof(top_line[0]), "DC IN: %.2fV",
               m_power.get_dc_in_voltage());
      snprintf(bottom_line[0], sizeof(bottom_line[0]), "DC ADJ: %.2fV",
               m_power.get_dc_adj_voltage());
      break;
    case MENU_STATE::OUT_MONITOR:
      snprintf(top_line[0], sizeof(top_line[0]), "Flap: %d%%", m_flap.get_flap());
      snprintf(bottom_line[0], sizeof(bottom_line[0]), "Out: %.2fV",
               m_power.get_dc_out_voltage());
      break;
    case MENU_STATE::INPUT_MONITOR: {
      const auto door = control.door();
      const uint8_t active = io.active_switches();
      snprintf(top_line[0], sizeof(top_line[0]), "Door: %s",
               door == Control::Door::TOP ? "TOP" : door == Control::Door::DOWN ? "DOWN" : "HALF");
      snprintf(bottom_line[0], sizeof(bottom_line[0]), "Upper:%d Lower:%d",
               !!(active & IO::UPPER), !!(active & IO::LOWER));
      break;
    }
    case MENU_STATE::IO_ERROR:
      snprintf(top_line[0], sizeof(top_line[0]), "%s",
               control.unexpected_io() ? "UNEXPECTED_IO" : "IO error: NONE");
      if (control.unexpected_io()) snprintf(bottom_line[0], sizeof(bottom_line[0]), "Press 0 to ack");
      break;
    case MENU_STATE::TIMEOUT_MONITOR_MIN:
      snprintf(top_line[0], sizeof(top_line[0]), "Timeout min:");
      snprintf(bottom_line[0], sizeof(bottom_line[0]), "%lu min left",
                control.get_time_left_mins());
      break;
    case MENU_STATE::TIMEOUT_MONITOR_SEC:
      snprintf(top_line[0], sizeof(top_line[0]), "Timeout sec:");
      snprintf(bottom_line[0], sizeof(bottom_line[0]), "%lu s left",
                control.get_time_left_sec());
      break;
    case MENU_STATE::POWER_ERROR: {
      snprintf(top_line[0], sizeof(top_line[0]), "Power error:");
      const char *error_name = nullptr;
      const Power::Error displayed_error =
          m_power_error_ack_required ? m_latched_power_error : power_error;
      switch (displayed_error) {
      case Power::Error::NONE:
        error_name = "NONE";
        break;
      case Power::Error::DC_IN_LOW:
        error_name = "DC_IN_LOW";
        break;
      case Power::Error::DC_ADJ_ERR:
        error_name = "DC_ADJ_ERR";
        break;
      case Power::Error::DC_OUT_ERR:
        error_name = "DC_OUT_ERR";
        break;
      }
      if (m_power_error_ack_required || power_error == Power::Error::NONE ||
          ((millis() - m_page_timestamp) / m_error_blink_interval_ms) % 2 == 0) {
        snprintf(bottom_line[0], sizeof(bottom_line[0]), "%s", error_name);
      }
      if (m_power_error_ack_required) {
        snprintf(top_line[1], sizeof(top_line[1]), "Press 0 to ack");
        pages = 2;
      }
      break;
    }
    case MENU_STATE::BLUETOOTH: {
      if (!m_bluetooth.is_active()) {
        snprintf(top_line[0], sizeof(top_line[0]), "BLE start fail");
        snprintf(bottom_line[0], sizeof(bottom_line[0]), "Press 0: retry");
        snprintf(top_line[1], sizeof(top_line[1]), "BLE start fail");
        snprintf(bottom_line[1], sizeof(bottom_line[1]), "NINA fw >= 3.0?");
        pages = 2;
        break;
      }
      const OtaCore &ota = m_bluetooth.ota();
      snprintf(top_line[0], sizeof(top_line[0]), "BLE: %s",
               m_bluetooth.is_connected() ? "connected" : "waiting");
      switch (ota.state()) {
      case OtaCore::State::IDLE:
        snprintf(bottom_line[0], sizeof(bottom_line[0]), "Ready");
        break;
      case OtaCore::State::RECEIVING:
        snprintf(bottom_line[0], sizeof(bottom_line[0]), "Update rx %u%%",
                 static_cast<unsigned>(ota.progress_percent()));
        break;
      case OtaCore::State::VERIFYING:
        snprintf(bottom_line[0], sizeof(bottom_line[0]), "Verifying");
        break;
      case OtaCore::State::READY:
        snprintf(bottom_line[0], sizeof(bottom_line[0]), "Rebooting...");
        break;
      case OtaCore::State::ERROR:
        snprintf(bottom_line[0], sizeof(bottom_line[0]), "Err %s",
                 OtaCore::error_name(ota.error()));
        break;
      }
      break;
    }
    case MENU_STATE::IDENTITY:
      snprintf(top_line[0], sizeof(top_line[0]), "Name:");
      snprintf(bottom_line[0], sizeof(bottom_line[0]), "%s",
               m_bluetooth.name());
      break;
    case MENU_STATE::ABOUT:
      snprintf(top_line[0], sizeof(top_line[0]), "(C) 2026");
      snprintf(bottom_line[0], sizeof(bottom_line[0]), "Laerdal Medical");
      snprintf(top_line[1], sizeof(top_line[1]), "Author:");
      snprintf(bottom_line[1], sizeof(bottom_line[1]), "Fredrik Savland");
      snprintf(top_line[2], sizeof(top_line[2]), "Version");
      snprintf(bottom_line[2], sizeof(bottom_line[2]), "%d.%d.%d",
               PARAMS_VERSION_MAJOR, PARAMS_VERSION_MINOR,
               PARAMS_VERSION_PATCH);
      snprintf(top_line[3], sizeof(top_line[3]), "CabControl");
      snprintf(bottom_line[3], sizeof(bottom_line[3]), "@ Github.com");
      snprintf(top_line[4], sizeof(top_line[4]), "Github.com/fsae");
      snprintf(bottom_line[4], sizeof(bottom_line[4]), "v/CabControl");
      pages = 5;
      break;
    default:
      snprintf(top_line[0], sizeof(top_line[0]), "Menu: Unknown");
      break;
    }
  }

  const uint8_t page_index =
      pages > 1 ? ((millis() - m_page_timestamp) / m_page_interval_ms) % pages : 0;

  m_display.clearDisplay();
  m_display.setTextSize(1);
  m_display.setTextColor(SSD1306_WHITE);
  m_display.setCursor(0, 0);
  m_display.println(top_line[page_index]);
  m_display.setCursor(0, 8);
  m_display.println(bottom_line[page_index]);
  m_display.display();

  m_display.dim(m_dimmed);
  if (inactive_ms > m_dim_timeout_ms) {
    m_dimmed = true;
  }
}

bool Oled::is_present_on_bus() const {
  Wire.beginTransmission(m_screen_address);
  return Wire.endTransmission() == 0;
}
