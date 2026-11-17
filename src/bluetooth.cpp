#include "bluetooth.h"
#include "fw_marker.h"

#include <Arduino.h>
#include <ArduinoBLE.h>
#include <SFU.h>
#include <string.h>

#include "hal/trng_api.h"

#if __has_include("ota_secret.h")
#include "ota_secret.h"
#else
#error "Missing include/ota_secret.h. Create it with: python3 tools/ble_flash.py --generate-secret"
#endif

extern "C" void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms);

namespace {

constexpr const char *SERVICE_UUID = "c0de0001-cab0-4c71-9a6e-3f1d2b7e8a10";
constexpr const char *INFO_UUID = "c0de0002-cab0-4c71-9a6e-3f1d2b7e8a10";
constexpr const char *CONTROL_UUID = "c0de0003-cab0-4c71-9a6e-3f1d2b7e8a10";
constexpr const char *DATA_UUID = "c0de0004-cab0-4c71-9a6e-3f1d2b7e8a10";
constexpr const char *STATUS_UUID = "c0de0005-cab0-4c71-9a6e-3f1d2b7e8a10";
constexpr const char *DIAG_SERVICE_UUID =
    "c0de0101-cab0-4c71-9a6e-3f1d2b7e8a10";
constexpr const char *DIAG_UUID = "c0de0102-cab0-4c71-9a6e-3f1d2b7e8a10";
constexpr const char *NAME_UUID = "c0de0103-cab0-4c71-9a6e-3f1d2b7e8a10";
constexpr const char *NAME_AUTH_UUID = "c0de0104-cab0-4c71-9a6e-3f1d2b7e8a10";
constexpr const char *NAME_SET_UUID = "c0de0105-cab0-4c71-9a6e-3f1d2b7e8a10";
constexpr int DATA_MAX_SIZE = 244;

BLEService ota_service(SERVICE_UUID);
BLECharacteristic info_characteristic(INFO_UUID, BLERead, OtaCore::INFO_SIZE,
                                      true);
BLECharacteristic control_characteristic(CONTROL_UUID, BLEWrite,
                                         OtaCore::CONTROL_MAX_SIZE);
BLECharacteristic data_characteristic(DATA_UUID, BLEWrite, DATA_MAX_SIZE);
BLECharacteristic status_characteristic(STATUS_UUID, BLERead | BLENotify,
                                        OtaCore::STATUS_SIZE, true);

BLEService diag_service(DIAG_SERVICE_UUID);
BLECharacteristic diag_characteristic(DIAG_UUID, BLERead | BLENotify,
                                      Diagnostics::SIZE, true);
BLECharacteristic name_characteristic(NAME_UUID, BLERead | BLENotify,
                                      IdentityCore::NAME_MAX_LEN);
BLECharacteristic name_auth_characteristic(NAME_AUTH_UUID,
                                           BLERead | BLENotify,
                                           IdentityCore::AUTH_SIZE, true);
BLECharacteristic name_set_characteristic(NAME_SET_UUID, BLEWrite,
                                          IdentityCore::SET_MAX_SIZE);

Bluetooth *s_instance = nullptr;

void fill_random(uint8_t *out, size_t len) {
  trng_t trng;
  trng_init(&trng);
  size_t produced = 0;
  while (produced < len) {
    size_t chunk = 0;
    if (trng_get_bytes(&trng, out + produced, len - produced, &chunk) != 0 ||
        chunk == 0) {
      break;
    }
    produced += chunk;
  }
  trng_free(&trng);
  // Never leave bytes unset if the TRNG misbehaves; the nonce must still be
  // unique per attempt.
  for (; produced < len; produced++) {
    out[produced] = static_cast<uint8_t>(micros() ^ (produced * 131));
  }
}

void handle_connected(BLEDevice) {
  if (s_instance != nullptr) {
    s_instance->on_connected();
  }
}

void handle_disconnected(BLEDevice) {
  if (s_instance != nullptr) {
    s_instance->on_disconnected();
  }
}

void handle_control_written(BLEDevice, BLECharacteristic characteristic) {
  if (s_instance != nullptr) {
    s_instance->on_control_written(characteristic.value(),
                                   characteristic.valueLength());
  }
}

void handle_data_written(BLEDevice, BLECharacteristic characteristic) {
  if (s_instance != nullptr) {
    s_instance->on_data_written(characteristic.value(),
                                characteristic.valueLength());
  }
}

void handle_name_set_written(BLEDevice, BLECharacteristic characteristic) {
  if (s_instance != nullptr) {
    s_instance->on_name_set_written(characteristic.value(),
                                    characteristic.valueLength());
  }
}

} // namespace

Bluetooth::Bluetooth()
    : m_ota(OTA_SECRET, sizeof(OTA_SECRET),
            {g_firmware_marker.major, g_firmware_marker.minor,
             g_firmware_marker.patch},
            g_firmware_marker.magic, "/ota", fill_random),
      m_identity(OTA_SECRET, sizeof(OTA_SECRET), "/ota", fill_random) {}

void Bluetooth::begin() {
  s_instance = this;
  m_storage_ready = SFU::begin() == 0;
  if (m_storage_ready) {
    m_identity.load();
  }
  start();
}

void Bluetooth::retry() {
  if (!m_active) {
    start();
  }
}

void Bluetooth::start() {
  m_start_failed = false;
  if (!BLE.begin()) {
    m_start_failed = true;
    return;
  }

  apply_name();
  BLE.setAdvertisedService(ota_service);
  // BLE.end() empties the services, so the characteristics are added on
  // every start.
  ota_service.addCharacteristic(info_characteristic);
  ota_service.addCharacteristic(control_characteristic);
  ota_service.addCharacteristic(data_characteristic);
  ota_service.addCharacteristic(status_characteristic);
  BLE.addService(ota_service);
  diag_service.addCharacteristic(diag_characteristic);
  diag_service.addCharacteristic(name_characteristic);
  diag_service.addCharacteristic(name_auth_characteristic);
  diag_service.addCharacteristic(name_set_characteristic);
  BLE.addService(diag_service);

  BLE.setEventHandler(BLEConnected, handle_connected);
  BLE.setEventHandler(BLEDisconnected, handle_disconnected);
  control_characteristic.setEventHandler(BLEWritten, handle_control_written);
  data_characteristic.setEventHandler(BLEWritten, handle_data_written);
  name_set_characteristic.setEventHandler(BLEWritten,
                                          handle_name_set_written);

  m_ota.reset();
  m_identity.reset();
  publish_ota();
  publish_identity();
  BLE.advertise();

  m_active = true;
  m_connected = false;
  m_advertising_stale = false;
  m_reboot_pending = false;
  m_activity_timestamp = millis();
}

void Bluetooth::apply_name() {
  m_advertised_name_index ^= 1;
  char *buffer = m_advertised_name[m_advertised_name_index];
  strncpy(buffer, m_identity.name(), IdentityCore::NAME_MAX_LEN);
  buffer[IdentityCore::NAME_MAX_LEN] = '\0';
  BLE.setLocalName(buffer);
  BLE.setDeviceName(buffer);
}

void Bluetooth::publish_ota() {
  uint8_t info[OtaCore::INFO_SIZE];
  m_ota.get_info(info);
  info_characteristic.writeValue(info, sizeof(info));

  uint8_t status[OtaCore::STATUS_SIZE];
  m_ota.get_status(status);
  status_characteristic.writeValue(status, sizeof(status));
}

void Bluetooth::publish_identity() {
  name_characteristic.writeValue(m_identity.name());
  uint8_t auth[IdentityCore::AUTH_SIZE];
  m_identity.get_auth(auth);
  name_auth_characteristic.writeValue(auth, sizeof(auth));
}

void Bluetooth::set_diagnostics(const Diagnostics &diagnostics) {
  if (!m_active) {
    return;
  }
  uint8_t encoded[Diagnostics::SIZE];
  diagnostics.encode(encoded);
  diag_characteristic.writeValue(encoded, sizeof(encoded));
}

void Bluetooth::on_connected() {
  m_connected = true;
  m_activity_timestamp = millis();
  m_ota.reset();
  m_identity.reset();
  publish_ota();
  publish_identity();
}

void Bluetooth::on_disconnected() {
  m_connected = false;
  m_activity_timestamp = millis();
  m_ota.reset();
  m_identity.reset();
  publish_ota();
  publish_identity();
}

void Bluetooth::on_control_written(const uint8_t *data, size_t len) {
  m_activity_timestamp = millis();
  m_ota.handle_control(data, len);
  publish_ota();
}

void Bluetooth::on_data_written(const uint8_t *data, size_t len) {
  m_activity_timestamp = millis();
  m_ota.handle_data(data, len);
  publish_ota();
}

void Bluetooth::on_name_set_written(const uint8_t *data, size_t len) {
  if (m_identity.handle_set(data, len)) {
    apply_name();
    // Changing the advertising data is deferred until the central has
    // disconnected; the controller rejects it during a connection.
    m_advertising_stale = true;
  }
  publish_identity();
}

void Bluetooth::iterate() {
  if (!m_active) {
    return;
  }
  BLE.poll();

  const uint32_t now = millis();
  if (m_ota.state() == OtaCore::State::READY) {
    if (!m_reboot_pending) {
      m_reboot_pending = true;
      m_reboot_timestamp = now;
    } else if (now - m_reboot_timestamp >= m_reboot_delay_ms) {
      BLE.disconnect();
      BLE.end();
      watchdog_reboot(0, 0, 10);
      while (true) {
      }
    }
    return;
  }

  if (m_advertising_stale && !m_connected) {
    m_advertising_stale = false;
    BLE.advertise();
  }

  if (m_ota.state() == OtaCore::State::RECEIVING &&
      now - m_activity_timestamp >= m_transfer_timeout_ms) {
    m_ota.fail(OtaCore::Error::TIMEOUT);
    publish_ota();
  }
}
