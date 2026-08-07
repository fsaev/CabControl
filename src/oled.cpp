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
#include "params.h"

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

Oled::Oled()
    : m_display(m_screen_width, m_screen_height, &Wire, m_oled_reset_pin),
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

  pinMode(A1, INPUT_PULLDOWN);
  pinMode(A2, INPUT_PULLDOWN);
  pinMode(A3, INPUT_PULLDOWN);
}

void Oled::iterate_sw() {

  m_sw_poll_timestamp = millis();

  static bool sw_left_prev = HIGH, sw_enter_prev = HIGH, sw_right_prev = HIGH;

  bool sw_left = digitalRead(SW_LEFT_PIN);
  bool sw_enter = digitalRead(SW_ENTER_PIN);
  bool sw_right = digitalRead(SW_RIGHT_PIN);

  // Detect press (transition from HIGH to LOW)
  if (sw_left_prev == HIGH && sw_left == LOW) {
    reset_dimmed();
    if (m_menu_state == static_cast<MENU_STATE>(static_cast<int>(MENU_STATE::STATE_START) + 1)) {
      m_menu_state =
          static_cast<MENU_STATE>(static_cast<int>(MENU_STATE::STATE_END) - 1);
    } else {
      m_menu_state =
          static_cast<MENU_STATE>(static_cast<int>(m_menu_state) - 1);
    }
  } else if (sw_enter_prev == HIGH && sw_enter == LOW) {
    reset_dimmed();
    // Handle enter switch press
  } else if (sw_right_prev == HIGH && sw_right == LOW) {
    reset_dimmed();
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

  sw_left_prev = sw_left;
  sw_enter_prev = sw_enter;
  sw_right_prev = sw_right;
}

float Oled::get_dc_in_voltage() const {
  return analogRead(A1) / 1023.0 *
         12.0; // Read the voltage from the potentiometer connected to A0
}

float Oled::get_dc_adj_voltage() const {
  return analogRead(A2) / 1023.0 *
         12.0; // Read the voltage from the potentiometer connected to A1
}

void Oled::iterate(const Flap &flap, const IO &io) {
  if (!m_initialized) {
    return;
  }

  char top_line[m_max_pages][m_line_length] = {};
  char bottom_line[m_max_pages][m_line_length] = {};
  uint8_t pages = 1;

  const std::vector<bool> inputs = io.get_inputs();

  if (m_welcome_display) {
    snprintf(top_line[0], sizeof(top_line[0]), "Welcome!");
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
               get_dc_in_voltage());
      snprintf(bottom_line[0], sizeof(bottom_line[0]), "DC ADJ: %.2fV",
               get_dc_adj_voltage());
      break;
    case MENU_STATE::OUT_MONITOR:
      snprintf(top_line[0], sizeof(top_line[0]), "Flap: %d%%", flap.get_flap());
      snprintf(bottom_line[0], sizeof(bottom_line[0]), "Out: %.2fV",
               flap.get_flap_voltage());
      break;
    case MENU_STATE::INPUT_MONITOR:
      snprintf(top_line[0], sizeof(top_line[0]), "Inputs:");
      snprintf(bottom_line[0], sizeof(bottom_line[0]), "%d %d %d %d",
               inputs[0] ? 1 : 0, inputs[1] ? 1 : 0, inputs[2] ? 1 : 0,
               inputs[3] ? 1 : 0);
      break;
    case MENU_STATE::TIMEOUT_MONITOR_MIN:
      snprintf(top_line[0], sizeof(top_line[0]), "Timeout min:");
      snprintf(bottom_line[0], sizeof(bottom_line[0]), "%lu min left",
                io.get_time_left_mins());
      break;
    case MENU_STATE::TIMEOUT_MONITOR_SEC:
      snprintf(top_line[0], sizeof(top_line[0]), "Timeout sec:");
      snprintf(bottom_line[0], sizeof(bottom_line[0]), "%lu s left",
                io.get_time_left_sec());
      break;
    case MENU_STATE::ABOUT:
      snprintf(top_line[0], sizeof(top_line[0]), "(C) 2026");
      snprintf(bottom_line[0], sizeof(bottom_line[0]), "Fredrik Savland");
      snprintf(top_line[1], sizeof(top_line[1]), "(C) 2026");
      snprintf(bottom_line[1], sizeof(bottom_line[1]), "Laerdal Medical");
      snprintf(top_line[2], sizeof(top_line[2]), "Version");
      snprintf(bottom_line[2], sizeof(bottom_line[2]), "%d.%d.%d",
               PARAMS_VERSION_MAJOR, PARAMS_VERSION_MINOR,
               PARAMS_VERSION_PATCH);
      snprintf(top_line[3], sizeof(top_line[3]), "CabControl");
      snprintf(bottom_line[3], sizeof(bottom_line[3]), "@ Github.com");
      sniprintf(top_line[4], sizeof(top_line[4]), "github.com");
      snprintf(bottom_line[4], sizeof(bottom_line[4]), "/fsaev/CabControl");
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
  if (millis() - m_dim_timestamp > m_dim_timeout_ms) {
    m_dimmed = true;
  }

  if (m_menu_state != MENU_STATE::STATE_START) {
    iterate_sw();
  }
}

bool Oled::is_present_on_bus() const {
  Wire.beginTransmission(m_screen_address);
  return Wire.endTransmission() == 0;
}
