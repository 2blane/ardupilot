#pragma once

#include <AP_HAL/AP_HAL.h>
#include <GCS_MAVLink/GCS_MAVLink.h>

namespace DroneShowFirmware {

enum class Target : uint8_t {
    WIFI = 1,
    ELRS = 2,
    SCREEN = 3,
};

bool start(Target target, bool skip_verify, uint8_t requester_sysid, uint8_t requester_compid);
bool busy();

}
