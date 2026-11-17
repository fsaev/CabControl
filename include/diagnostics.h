#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <stddef.h>
#include <stdint.h>

// Snapshot published on the BLE diagnostics characteristic.
// Encoded (20 B, little-endian):
//   [format][uptime s u32][dc_in mV u16][dc_adj mV u16][dc_out mV u16]
//   [flags][power error][latched power error][flap %][level]
//   [timeout level][time left s u16][inputs]
// Enum values follow Power::Error and Control::Levels. inputs holds the raw
// pin levels (bit 0 OFF, 1 HALF, 2 FULL, 3 AUX; 1 = HIGH = not active).
struct Diagnostics {
  static constexpr uint8_t FORMAT_VERSION = 1;
  static constexpr size_t SIZE = 20;

  enum Flag : uint8_t {
    DC_ON = 0x01,
    DAC_PRESENT = 0x02,
    POWER_ACK_PENDING = 0x04,
    OLED_PRESENT = 0x08
  };

  uint32_t uptime_s = 0;
  float dc_in_v = 0;
  float dc_adj_v = 0;
  float dc_out_v = 0;
  uint8_t flags = 0;
  uint8_t power_error = 0;
  uint8_t latched_power_error = 0;
  uint8_t flap_percent = 0;
  uint8_t level = 0;
  uint8_t timeout_level = 0;
  uint32_t time_left_s = 0;
  uint8_t inputs = 0;

  void encode(uint8_t out[SIZE]) const;
};

#endif
