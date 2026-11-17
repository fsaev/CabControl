#include "fw_marker.h"
#include "params.h"

// This must be the only place the magic bytes appear in the source so that the
// image contains exactly one marker.
extern const FirmwareMarker g_firmware_marker __attribute__((used)) = {
    {'C', 'C', 'F', 'W', 'V', 'E', 'R', '1'},
    PARAMS_VERSION_MAJOR,
    PARAMS_VERSION_MINOR,
    PARAMS_VERSION_PATCH,
    0};
