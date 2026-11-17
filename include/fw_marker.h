#ifndef FW_MARKER_H
#define FW_MARKER_H

#include <stdint.h>

// Embedded once in the firmware image so tools/ble_flash.py and the OTA
// verifier can read the image version from a raw firmware.bin.
struct FirmwareMarker {
  uint8_t magic[8];
  uint8_t major;
  uint8_t minor;
  uint8_t patch;
  uint8_t reserved;
};

extern const FirmwareMarker g_firmware_marker;

#endif
