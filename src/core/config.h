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

    // Smoothing. Selected per connection from the packet source address: a
    // tracker on this machine (loopback) uses localSmoothing, a remote network
    // device uses remoteSmoothing. Both cover rotation and position.
    float localSmoothing = 0.0f;
    float remoteSmoothing = 0.15f;

    // Hotkeys (Virtual Key codes)
    int toggleKey = DEFAULT_TOGGLE_KEY;
    int positionToggleKey = DEFAULT_POSITION_TOGGLE_KEY;
    int yawModeKey = DEFAULT_YAW_MODE_KEY;
    int diagnosticMarkerKey = DEFAULT_DIAGNOSTIC_MARKER_KEY;

    // Position (6DOF)
    float positionSensitivityX = 1.0f;
    float positionSensitivityY = 1.0f;
    float positionSensitivityZ = 1.0f;
    float positionLimitX = 0.30f;
    float positionLimitY = 0.20f;
    float positionLimitZ = 0.40f;
    float positionLimitZBack = 0.10f;
    bool positionInvertX = true;
    bool positionInvertY = false;
    bool positionInvertZ = false;
    bool positionEnabled = true;

    // Flashlight. The beam is rotated by the head pose scaled by
    // flashlightMultiplier, so it leads the view instead of matching it. This
    // is a game-specific light-to-view relationship, not tracker pose shaping.
    bool flashlightTracking = true;
    float flashlightMultiplier = 1.5f;

    // General
    bool autoEnable = true;
    bool worldSpaceYaw = true;

    bool Load(const char* path);
    bool Save(const char* path) const;
    void SetDefaults();
    void Validate();
};

} // namespace RE9HT
