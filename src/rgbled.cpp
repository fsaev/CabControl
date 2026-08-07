#include "rgbled.h"
#include <Arduino.h>
#include <WiFiNINA.h>

void RGBLED::init() {
    pinMode(LEDR, OUTPUT);
    pinMode(LEDG, OUTPUT);
    pinMode(LEDB, OUTPUT);
}

void RGBLED::set_color(uint8_t red, uint8_t green, uint8_t blue) {
    analogWrite(LEDR, 255 - red);
    analogWrite(LEDG, 255 - green);
    analogWrite(LEDB, 255 - blue);
}

void RGBLED::set_color(uint32_t color) {
    uint8_t red = (color >> 16) & 0xFF;
    uint8_t green = (color >> 8) & 0xFF;
    uint8_t blue = color & 0xFF;
    set_color(red, green, blue);
}

void RGBLED::turn_off() {
    set_color(0, 0, 0);
}