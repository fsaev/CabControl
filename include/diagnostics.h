#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <stddef.h>
#include <stdint.h>

// Snapshot published on the BLE diagnostics characteristic.
// Encoded (20 B, little-endian):
//   [format][uptime s u32][dc_in mV u16][dc_adj mV u16][dc_out mV u16]
//   [flags][power error][latched power error][flap %][Control::FlapState]
//   [Control::Door][time left s u16][active switches: bit 0 upper, 1 lower]
struct Diagnostics {
  static constexpr uint8_t FORMAT_VERSION = 2;
  static constexpr size_t SIZE = 20;

  enum Flag : uint8_t {
    DC_ON = 0x01,
    DAC_PRESENT = 0x02,
    POWER_ACK_PENDING = 0x04,
    OLED_PRESENT = 0x08,
    UNEXPECTED_IO = 0x10
  };

  uint32_t uptime_s = 0;
  float dc_in_v = 0;
  float dc_adj_v = 0;
  float dc_out_v = 0;
  uint8_t flags = 0;
  uint8_t power_error = 0;
  uint8_t latched_power_error = 0;
  uint8_t flap_percent = 0;
  uint8_t flap_state = 0;
  uint8_t door = 0;
  uint32_t time_left_s = 0;
  uint8_t inputs = 0;

  void encode(uint8_t out[SIZE]) const;
};

#endif
