#include "pch.h"
#include "config.h"
#include "logger.h"

#include <cameraunlock/config/ini_reader.h>

#include <algorithm>

namespace RE9HT {

void Config::SetDefaults() {
    *this = Config{};
}

void Config::Validate() {
    yawMultiplier = std::clamp(yawMultiplier, 0.1f, 5.0f);
    pitchMultiplier = std::clamp(pitchMultiplier, 0.1f, 5.0f);
    rollMultiplier = std::clamp(rollMultiplier, 0.0f, 2.0f);

    positionSensitivityX = std::clamp(positionSensitivityX, 0.1f, 10.0f);
    positionSensitivityY = std::clamp(positionSensitivityY, 0.1f, 10.0f);
    positionSensitivityZ = std::clamp(positionSensitivityZ, 0.1f, 10.0f);

    positionLimitX = std::clamp(positionLimitX, 0.01f, 2.0f);
    positionLimitY = std::clamp(positionLimitY, 0.01f, 2.0f);
    positionLimitZ = std::clamp(positionLimitZ, 0.01f, 2.0f);
    positionLimitZBack = std::clamp(positionLimitZBack, 0.01f, 2.0f);
    positionSmoothing = std::clamp(positionSmoothing, 0.0f, 0.99f);

    if (udpPort < 1024) {
        Logger::Instance().Warning("UDP port %d is in reserved range, using default %d",
                                   udpPort, DEFAULT_UDP_PORT);
        udpPort = DEFAULT_UDP_PORT;
    }
}

bool Config::Load(const char* path) {
    SetDefaults();

    cameraunlock::IniReader reader;
    if (!reader.Open(path)) {
        Logger::Instance().Warning("Could not load config from %s, using defaults", path);
        return false;
    }

    udpPort = static_cast<uint16_t>(reader.ReadInt("Network", "UDPPort", udpPort));

    yawMultiplier = reader.ReadFloat("Sensitivity", "YawMultiplier", yawMultiplier);
    pitchMultiplier = reader.ReadFloat("Sensitivity", "PitchMultiplier", pitchMultiplier);
    rollMultiplier = reader.ReadFloat("Sensitivity", "RollMultiplier", rollMultiplier);

    toggleKey = reader.ReadHex("Hotkeys", "ToggleKey", toggleKey);
    recenterKey = reader.ReadHex("Hotkeys", "RecenterKey", recenterKey);
    positionToggleKey = reader.ReadHex("Hotkeys", "PositionToggleKey", positionToggleKey);
    yawModeKey = reader.ReadHex("Hotkeys", "YawModeKey", yawModeKey);
    diagnosticMarkerKey = reader.ReadHex("Hotkeys", "DiagnosticMarkerKey", diagnosticMarkerKey);

    positionSensitivityX = reader.ReadFloat("Position", "SensitivityX", positionSensitivityX);
    positionSensitivityY = reader.ReadFloat("Position", "SensitivityY", positionSensitivityY);
    positionSensitivityZ = reader.ReadFloat("Position", "SensitivityZ", positionSensitivityZ);
    positionLimitX = reader.ReadFloat("Position", "LimitX", positionLimitX);
    positionLimitY = reader.ReadFloat("Position", "LimitY", positionLimitY);
    positionLimitZ = reader.ReadFloat("Position", "LimitZ", positionLimitZ);
    positionLimitZBack = reader.ReadFloat("Position", "LimitZBack", positionLimitZBack);
    positionSmoothing = reader.ReadFloat("Position", "Smoothing", positionSmoothing);
    positionInvertX = reader.ReadBool("Position", "InvertX", positionInvertX);
    positionInvertY = reader.ReadBool("Position", "InvertY", positionInvertY);
    positionInvertZ = reader.ReadBool("Position", "InvertZ", positionInvertZ);
    positionEnabled = reader.ReadBool("Position", "Enabled", positionEnabled);

    autoEnable = reader.ReadBool("General", "AutoEnable", autoEnable);
    worldSpaceYaw = reader.ReadBool("General", "WorldSpaceYaw", worldSpaceYaw);

    Validate();
    Logger::Instance().Info("Config loaded from %s", path);
    return true;
}

bool Config::Save(const char* path) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        Logger::Instance().Error("Failed to save config to %s", path);
        return false;
    }

    file << "; RE9 Head Tracking Configuration\n";
    file << "; Delete this file to reset to defaults\n\n";

    file << "[Network]\n";
    file << "; UDP port for OpenTrack data (default: 4242)\n";
    file << "UDPPort=" << udpPort << "\n\n";

    file << "[Sensitivity]\n";
    file << "; Rotation sensitivity multipliers (1.0 = 1:1)\n";
    file << "YawMultiplier=" << yawMultiplier << "\n";
    file << "PitchMultiplier=" << pitchMultiplier << "\n";
    file << "RollMultiplier=" << rollMultiplier << "\n\n";

    file << "[Position]\n";
    file << "; Position tracking sensitivity (0.1-10.0, higher = more movement)\n";
    file << "SensitivityX=" << positionSensitivityX << "\n";
    file << "SensitivityY=" << positionSensitivityY << "\n";
    file << "SensitivityZ=" << positionSensitivityZ << "\n";
    file << "; Position limits in meters\n";
    file << "LimitX=" << positionLimitX << "\n";
    file << "LimitY=" << positionLimitY << "\n";
    file << "LimitZ=" << positionLimitZ << "\n";
    file << "LimitZBack=" << positionLimitZBack << "\n";
    file << "Smoothing=" << positionSmoothing << "\n";
    file << "InvertX=" << (positionInvertX ? "true" : "false") << "\n";
    file << "InvertY=" << (positionInvertY ? "true" : "false") << "\n";
    file << "InvertZ=" << (positionInvertZ ? "true" : "false") << "\n";
    file << "Enabled=" << (positionEnabled ? "true" : "false") << "\n\n";

    file << "[Hotkeys]\n";
    file << "; Virtual key codes (hex)\n";
    file << "ToggleKey=0x" << std::hex << toggleKey << "    ; End\n";
    file << "RecenterKey=0x" << std::hex << recenterKey << "  ; Home\n";
    file << "PositionToggleKey=0x" << std::hex << positionToggleKey << " ; Page Up\n";
    file << "YawModeKey=0x" << std::hex << yawModeKey << "      ; Page Down - toggle world/local yaw\n";
    file << "DiagnosticMarkerKey=0x" << std::hex << diagnosticMarkerKey << " ; F9 - place marker in diagnostic log\n\n";

    file << "[General]\n";
    file << "AutoEnable=" << (autoEnable ? "true" : "false") << "\n";
    file << "; Yaw mode: false = camera-local, true = horizon-locked (default)\n";
    file << "WorldSpaceYaw=" << (worldSpaceYaw ? "true" : "false") << "\n";

    file.close();
    Logger::Instance().Info("Config saved to %s", path);
    return true;
}

} // namespace RE9HT
