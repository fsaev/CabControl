#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include "diagnostics.h"
#include "identity_core.h"
#include "ota_core.h"
#include <stdint.h>

// Bluetooth LE, running from boot. Advertises the identification name and
// firmware version (manufacturer data) and
// provides two services:
// - firmware update (OtaCore): a verified image is handed to the Arduino SFU
//   bootloader, which installs it after the reboot;
// - diagnostics (Diagnostics) and the authenticated name setting
//   (IdentityCore).
// The NINA module runs BLE, so the RGB LED is not available.
class Bluetooth {
public:
  Bluetooth();

  // Mounts the flash filesystem, loads the name and starts BLE.
  void begin();
  // Starts BLE again after a failed begin().
  void retry();
  void iterate();
  void set_diagnostics(const Diagnostics &diagnostics);

  bool is_active() const { return m_active; }
  bool is_connected() const { return m_connected; }
  bool start_failed() const { return m_start_failed; }
  bool storage_ready() const { return m_storage_ready; }
  const char *name() const { return m_identity.name(); }
  const OtaCore &ota() const { return m_ota; }

  void on_connected();
  void on_disconnected();
  void on_control_written(const uint8_t *data, size_t len);
  void on_data_written(const uint8_t *data, size_t len);
  void on_name_set_written(const uint8_t *data, size_t len);

private:
  static constexpr uint32_t m_transfer_timeout_ms = 30UL * 1000UL;
  static constexpr uint32_t m_reboot_delay_ms = 1000;

  void start();
  void publish_ota();
  void publish_identity();
  void apply_name();

  OtaCore m_ota;
  IdentityCore m_identity;
  // ArduinoBLE keeps a pointer to the local name and needs the previous one
  // intact when it changes, so two buffers alternate.
  char m_advertised_name[2][IdentityCore::NAME_MAX_LEN + 1] = {};
  uint8_t m_advertised_name_index = 0;
  bool m_advertising_stale = false;
  bool m_active = false;
  bool m_connected = false;
  bool m_start_failed = false;
  bool m_storage_ready = false;
  bool m_reboot_pending = false;
  uint32_t m_activity_timestamp = 0;
  uint32_t m_reboot_timestamp = 0;
};

#endif
