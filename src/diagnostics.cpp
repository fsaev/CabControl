#include "diagnostics.h"

namespace {

void put_u16(uint8_t *p, uint32_t value) {
  if (value > 0xFFFF) {
    value = 0xFFFF;
  }
  p[0] = static_cast<uint8_t>(value);
  p[1] = static_cast<uint8_t>(value >> 8);
}

void put_u32(uint8_t *p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value);
  p[1] = static_cast<uint8_t>(value >> 8);
  p[2] = static_cast<uint8_t>(value >> 16);
  p[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t to_millivolts(float volts) {
  if (!(volts > 0.0f)) {
    return 0;
  }
  return static_cast<uint32_t>(volts * 1000.0f + 0.5f);
}

} // namespace

void Diagnostics::encode(uint8_t out[SIZE]) const {
  out[0] = FORMAT_VERSION;
  put_u32(out + 1, uptime_s);
  put_u16(out + 5, to_millivolts(dc_in_v));
  put_u16(out + 7, to_millivolts(dc_adj_v));
  put_u16(out + 9, to_millivolts(dc_out_v));
  out[11] = flags;
  out[12] = power_error;
  out[13] = latched_power_error;
  out[14] = flap_percent;
  out[15] = level;
  out[16] = timeout_level;
  put_u16(out + 17, time_left_s);
  out[19] = inputs;
}
