#ifndef POWER_H
#define POWER_H

#include <stdint.h>
#include <vector>
#include <Arduino.h>

class Flap;

class Power {
public:
  enum class Error { NONE, DC_IN_LOW, DC_ADJ_ERR, DC_OUT_ERR };

  Power();
  float get_dc_in_voltage() const;
  float get_dc_adj_voltage() const;
  float get_dc_out_voltage() const;
  bool is_dc_on() const {
    return digitalRead(PIN_DC_SHDN) == HIGH;
  }
  Error get_error() const;
  void iterate(const Flap &flap);

private:
  Error m_error = Error::NONE;
  static constexpr float DC_IN_VOLTAGE_THRESHOLD = 11.0; // Minimum allowed DC_IN voltage

    static constexpr float DC_ADJ_VOLTAGE_NOMINAL = 10.0; // Nominal DC_ADJ voltage
  static constexpr float DC_ADJ_MARGIN = 0.05; // Allowed margin for DC_ADJ voltage in percent

  static constexpr float DC_OUT_MARGIN = 0.10; // 10% of DC_ADJ scaled by flap percentage

  static constexpr uint8_t PIN_DC_IN = A1;
  static constexpr uint8_t PIN_DC_ADJ = A2;
  static constexpr uint8_t PIN_DC_OUT = A3;
  static constexpr uint8_t PIN_DC_SHDN = 2;

  static constexpr float DC_IN_CALIB_ADJ = 0.3;
  static constexpr float DC_ADJ_CALIB_ADJ = 0.3;
  static constexpr float DC_OUT_CALIB_ADJ = 0.3;

  uint32_t m_power_timestamp = 0;
  static constexpr uint16_t m_power_refresh_interval_ms = 100; // Timeout duration in milliseconds

  uint32_t m_power_settle_timestamp = 0;
  static constexpr uint16_t m_power_settle_interval_ms = 2000; // Settle duration in milliseconds
  bool m_power_settled = false;
};

#endif
