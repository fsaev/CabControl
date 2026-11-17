#include "power.h"
#include "flap.h"
#include <Arduino.h>

Power::Power() {
  pinMode(PIN_DC_IN, INPUT_PULLDOWN);
  pinMode(PIN_DC_ADJ, INPUT_PULLDOWN);
  pinMode(PIN_DC_OUT, INPUT_PULLDOWN);

  pinMode(PIN_DC_SHDN, OUTPUT);
  digitalWrite(PIN_DC_SHDN, HIGH);

  m_power_settle_timestamp = millis();
}

float Power::get_dc_in_voltage() const {
  return (analogRead(PIN_DC_IN) / 1023.0 *
         12.0) + DC_IN_CALIB_ADJ; // Read the voltage from the potentiometer connected to A0
}

float Power::get_dc_adj_voltage() const {
  return (analogRead(PIN_DC_ADJ) / 1023.0 *
         12.0) + DC_ADJ_CALIB_ADJ; // Read the voltage from the potentiometer connected to A1
}

float Power::get_dc_out_voltage() const {
  return (analogRead(PIN_DC_OUT) / 1023.0 *
         12.0) + DC_OUT_CALIB_ADJ; // Read the voltage from the potentiometer connected to A3
}

Power::Error Power::get_error() const {
  return m_error;
}

void Power::iterate(const Flap &flap) {
  if ((millis() - m_power_settle_timestamp < m_power_settle_interval_ms) &&
      !m_power_settled) {
    return; // Wait for the power to settle
  } else {
    m_power_settled = true;
  }

  

  if (millis() - m_power_timestamp > m_power_refresh_interval_ms) {
    m_power_timestamp = millis();

    float dc_in = get_dc_in_voltage();
    float dc_adj = get_dc_adj_voltage();
    float dc_out = get_dc_out_voltage();
    const float expected_dc_out = dc_adj * (flap.get_flap() / 100.0f);

    if (dc_in < DC_IN_VOLTAGE_THRESHOLD) {
      m_error = Error::DC_IN_LOW;
      digitalWrite(PIN_DC_SHDN, LOW); // Shutdown the power if DC_IN is low
    } else if ((abs(dc_adj - DC_ADJ_VOLTAGE_NOMINAL) > DC_ADJ_MARGIN * DC_ADJ_VOLTAGE_NOMINAL) &&
               is_dc_on()) {
      m_error = Error::DC_ADJ_ERR;
    } else if ((abs(dc_out - expected_dc_out) > DC_OUT_MARGIN * expected_dc_out) &&
               is_dc_on()) {
      m_error = Error::DC_OUT_ERR;
    } else {
      m_error = Error::NONE;
      digitalWrite(PIN_DC_SHDN, HIGH); // Restore power if all voltages are within acceptable range
    }
  }
}
