#/**************************************************************************
 This is an example for our Monochrome OLEDs based on SSD1306 drivers

 Pick one up today in the adafruit shop!
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

#include "bluetooth.h"
#include "control.h"
#include "diagnostics.h"
#include "flap.h"
#include "io.h"
#include "oled.h"
#include <Arduino.h>

Flap flap;
IO io;
Power power;
Bluetooth bluetooth;
Oled oled(power, flap, bluetooth);
Control control(io, flap);

uint32_t start = millis();
uint32_t diagnostics_timestamp = 0;
constexpr uint32_t DIAGNOSTICS_INTERVAL_MS = 500;

void setup() {
  flap.init();
  control.begin();
  oled.init();
  bluetooth.begin();
  pinMode(13, OUTPUT);

  Serial.begin(115200);
  //flap.set_dac_direct(50 * 40); // Convert 0-100
  //delay(5000);
}

void debug_dac_sweep() {
  static uint8_t dac_value = 0;
  static bool increasing = true;
  flap.set_dac_direct(dac_value * 40); // Convert 0-100
  Serial.print("DAC Value: ");
  Serial.println(dac_value);
  if (increasing) {
    dac_value += 1;
    if (dac_value >= 100) {
      increasing = false;
    }
  } else {
    dac_value -= 1;
    if (dac_value <= 0) {
      increasing = true;
    }
  }
}

uint32_t uptime_seconds() {
  // Accumulated in 64 bits so the uptime survives the 49-day millis() wrap.
  static uint32_t last_ms = 0;
  static uint64_t total_ms = 0;
  const uint32_t now = millis();
  total_ms += now - last_ms;
  last_ms = now;
  return static_cast<uint32_t>(total_ms / 1000);
}

void publish_diagnostics() {
  Diagnostics diagnostics;
  diagnostics.uptime_s = uptime_seconds();
  diagnostics.dc_in_v = power.get_dc_in_voltage();
  diagnostics.dc_adj_v = power.get_dc_adj_voltage();
  diagnostics.dc_out_v = power.get_dc_out_voltage();
  diagnostics.flags =
      (power.is_dc_on() ? Diagnostics::DC_ON : 0) |
      (flap.is_dac_present() ? Diagnostics::DAC_PRESENT : 0) |
      (oled.power_error_ack_pending() ? Diagnostics::POWER_ACK_PENDING : 0) |
      (oled.is_initialized() ? Diagnostics::OLED_PRESENT : 0) |
      (control.unexpected_io() ? Diagnostics::UNEXPECTED_IO : 0);
  diagnostics.power_error = static_cast<uint8_t>(power.get_error());
  diagnostics.latched_power_error =
      static_cast<uint8_t>(oled.latched_power_error());
  diagnostics.flap_percent = flap.get_flap();
  diagnostics.flap_state = static_cast<uint8_t>(control.flap_state());
  diagnostics.door = static_cast<uint8_t>(control.door());
  diagnostics.time_left_s = control.get_time_left_sec();
  diagnostics.inputs = io.active_switches();
  bluetooth.set_diagnostics(diagnostics);
}

void loop() {
  bluetooth.iterate();
  if (millis() - diagnostics_timestamp >= DIAGNOSTICS_INTERVAL_MS) {
    diagnostics_timestamp = millis();
    publish_diagnostics();
  }
  if (millis() - start > 5) {
    //debug_dac_sweep();
    start = millis();
    oled.iterate(control, io);
    power.iterate(flap);

    control.iterate();
  }
}
