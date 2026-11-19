#pragma once

#include <AP_HAL/AP_HAL.h>
#include <GCS_MAVLink/GCS_MAVLink.h>

namespace DroneShowFirmware {

constexpr uint8_t ELRS_SERIAL_INDEX = 4;

enum class Target : uint8_t {
    // Value 1 is retired; preserve the existing ELRS/screen command IDs.
    ELRS = 2,
    SCREEN = 3,
};

bool start(Target target, bool skip_verify, uint8_t requester_sysid, uint8_t requester_compid);
bool busy();

}
