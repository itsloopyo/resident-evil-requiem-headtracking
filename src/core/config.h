#pragma once

#include <cstdint>

namespace RE9HT {

struct Config {
    // Network
    uint16_t udpPort = DEFAULT_UDP_PORT;

    // Sensitivity
    float yawMultiplier = 1.0f;
    float pitchMultiplier = 1.0f;
    float rollMultiplier = 1.0f;

    // Hotkeys (Virtual Key codes)
    int toggleKey = DEFAULT_TOGGLE_KEY;
    int recenterKey = DEFAULT_RECENTER_KEY;
    int positionToggleKey = DEFAULT_POSITION_TOGGLE_KEY;
    int yawModeKey = DEFAULT_YAW_MODE_KEY;
    int diagnosticMarkerKey = DEFAULT_DIAGNOSTIC_MARKER_KEY;

    // Position (6DOF)
    float positionSensitivityX = 2.0f;
    float positionSensitivityY = 2.0f;
    float positionSensitivityZ = 2.0f;
    float positionLimitX = 0.30f;
    float positionLimitY = 0.20f;
    float positionLimitZ = 0.40f;
    float positionLimitZBack = 0.10f;
    float positionSmoothing = 0.15f;
    bool positionInvertX = true;
    bool positionInvertY = false;
    bool positionInvertZ = false;
    bool positionEnabled = true;

    // General
    bool autoEnable = true;
    bool worldSpaceYaw = true;

    bool Load(const char* path);
    bool Save(const char* path) const;
    void SetDefaults();
    void Validate();
};

} // namespace RE9HT
