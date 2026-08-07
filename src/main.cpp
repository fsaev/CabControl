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

#include "flap.h"
#include "io.h"
#include "oled.h"
#include "rgbled.h"
#include <Arduino.h>

Flap flap;
IO io;
Oled oled;
RGBLED rgbled;

uint32_t start = millis();

void setup() {
  rgbled.init();
  rgbled.set_color(RGBLED::Colors::CYAN);
  flap.init();
  oled.init();
  pinMode(13, OUTPUT);
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);

  Serial.begin(115200);
}

void debug_dac_sweep() {
  static uint8_t dac_value = 0;
  static bool increasing = true;
  flap.set_dac_direct(dac_value * 40); // Convert 0-100
  if (increasing) {
    dac_value++;
    if (dac_value >= 100) {
      increasing = false;
    }
  } else {
    dac_value--;
    if (dac_value == 0) {
      increasing = true;
    }
  }
}

void loop() {
  if (millis() - start > 100) {
    start = millis();
    oled.iterate(flap, io);
    io.iterate();
    switch (io.get_level()) {
    case IO::Levels::OFF:
      rgbled.set_color(RGBLED::Colors::RED);
      flap.set_flap(0);
      break;
    case IO::Levels::HALF:
      rgbled.set_color(RGBLED::Colors::YELLOW);
      flap.set_flap(50);
      break;
    case IO::Levels::FULL:
      rgbled.set_color(RGBLED::Colors::GREEN);
      flap.set_flap(100);
      break;
    case IO::Levels::TIMEOUT:
      rgbled.set_color(RGBLED::Colors::BLUE);
      flap.set_flap(0);
      break;
    case IO::Levels::UNKNOWN:
      rgbled.set_color(RGBLED::Colors::CYAN);
      flap.set_flap(0);
      break;
    }
  }
}
