#include "flap.h"
#include <Arduino.h>
#include <Wire.h>

namespace {
constexpr uint8_t kMcp4725Address = 0x60;

bool dac_present_on_bus() {
    Wire.beginTransmission(kMcp4725Address);
    return Wire.endTransmission() == 0;
}
}

Flap::Flap() : m_flap_value(0), m_flap_updated(false), m_dac_initialized(false) {

}

void Flap::init() {
    Wire.begin();
    if (!dac_present_on_bus()) {
        m_dac_initialized = false;
        return;
    }
    m_dac.begin(kMcp4725Address);
    m_dac_initialized = true;

}

void Flap::set_flap(uint8_t flap) {
    if (flap > 100) {
        flap = 100;
    }
    if (m_flap_value != flap) {
        m_flap_value = flap;
        m_flap_updated = true;
    }
}

uint8_t Flap::get_flap() const {
    return m_flap_value;
}

float Flap::get_flap_voltage() const {
    float voltage = analogRead(A3) / 1023.0 * 12.0; // Read the voltage from the potentiometer connected to A3
    return voltage;

}

void Flap::set_dac_direct(uint16_t value) {
    if (m_dac_initialized) {
        m_dac.setVoltage(value, false);
    }
}

void Flap::write_flap_value() {
    if(m_flap_updated && m_dac_initialized) {
        // Code to write m_flap_value to the OLED display
        m_flap_updated = false; // Reset the update flag after writing

        // Send SPI commands to DAC
        Serial.print("Writing flap value to DAC: ");
        Serial.println(m_flap_value);
        m_dac.setVoltage(m_flap_value * 40, false); // Convert 0-100% to 0-4095 for 12-bit DAC
    }
}
