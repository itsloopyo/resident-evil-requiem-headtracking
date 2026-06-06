#include "pch.h"
#include "mod.h"
#include "logger.h"
#include "camera/game_state_detector.h"
#include "camera/gui_compensation.h"

#include <cameraunlock/time/qpc_clock.h>

namespace RE9HT {

using cameraunlock::TrackingMode;

Mod& Mod::Instance() {
    static Mod instance;
    return instance;
}

bool Mod::Initialize() {
    if (m_initialized.load()) {
        Logger::Instance().Warning("Mod already initialized");
        return true;
    }

    Logger::Instance().Info("RE9 Head Tracking v%s initializing...", RE9HT_VERSION);

    // Determine plugin directory (used for config + diagnostic log)
    HMODULE hModule = nullptr;
    char dllPath[MAX_PATH] = {};
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&Mod::Instance, &hModule)) {
        GetModuleFileNameA(hModule, dllPath, MAX_PATH);
    }
    m_pluginDir.assign(dllPath);
    auto lastSlash = m_pluginDir.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        m_pluginDir = m_pluginDir.substr(0, lastSlash + 1);
    }

    if (!LoadConfig()) {
        Logger::Instance().Warning("Using default configuration");
    }

    cameraunlock::SensitivitySettings sensitivity;
    sensitivity.yaw = m_config.yawMultiplier;
    sensitivity.pitch = m_config.pitchMultiplier;
    sensitivity.roll = m_config.rollMultiplier;
    m_session.GetProcessor().SetSensitivity(sensitivity);

    Logger::Instance().Info("Sensitivity: yaw=%.2f pitch=%.2f roll=%.2f",
                            sensitivity.yaw, sensitivity.pitch, sensitivity.roll);

    m_session.SetMode(m_config.positionEnabled ? TrackingMode::RotationAndPosition
                                               : TrackingMode::RotationOnly);
    m_worldSpaceYaw = m_config.worldSpaceYaw;

    cameraunlock::PositionSettings posSettings(
        m_config.positionSensitivityX, m_config.positionSensitivityY, m_config.positionSensitivityZ,
        m_config.positionLimitX, m_config.positionLimitY, m_config.positionLimitZ, m_config.positionLimitZBack,
        m_config.positionSmoothing,
        m_config.positionInvertX, m_config.positionInvertY, m_config.positionInvertZ
    );
    m_session.GetPositionProcessor().SetSettings(posSettings);
    // The previous per-mod pipeline never engaged tracker pivot compensation
    // (it passed radians to a degrees API, zeroing the artifact). Keep that
    // tuning until pivot compensation is verified in game.
    m_session.GetPositionProcessor().SetTrackerPivotForward(0.0f);

    Logger::Instance().Info("Position: %s, sens=%.1f/%.1f/%.1f",
                            IsPositionEnabled() ? "6DOF" : "3DOF",
                            posSettings.sensitivity_x, posSettings.sensitivity_y, posSettings.sensitivity_z);

    // Start UDP receiver
    if (!m_udpReceiver.Start(m_config.udpPort)) {
        Logger::Instance().Error("UDP receiver failed to start on port %d", m_config.udpPort);
        return false;
    }
    Logger::Instance().Info("UDP receiver started on port %d", m_config.udpPort);

    if (m_config.autoEnable) {
        m_enabled.store(true);
        Logger::Instance().Info("Head tracking auto-enabled");
    }

    InitDiagnosticLog();

    m_initialized.store(true);
    Logger::Instance().Info("Initialization complete");
    return true;
}

void Mod::Shutdown() {
    if (!m_initialized.load()) return;

    Logger::Instance().Info("Shutting down...");
    if (m_diagFile) {
        fflush(m_diagFile);
        fclose(m_diagFile);
        m_diagFile = nullptr;
        Logger::Instance().Info("Diagnostic log closed");
    }
    m_udpReceiver.Stop();
    m_initialized.store(false);
    Logger::Instance().Info("Shutdown complete");
}

bool Mod::LoadConfig() {
    std::string configPath = m_pluginDir + "HeadTracking.ini";

    if (!m_config.Load(configPath.c_str())) {
        m_config.SetDefaults();
        m_config.Save(configPath.c_str());
        return false;
    }
    return true;
}

void Mod::SetEnabled(bool enabled) {
    bool wasEnabled = m_enabled.exchange(enabled);
    if (wasEnabled != enabled) {
        Logger::Instance().Info("Head tracking %s", enabled ? "enabled" : "disabled");
    }
}

void Mod::Toggle() {
    SetEnabled(!m_enabled.load());
}

void Mod::Recenter() {
    m_session.Recenter();
    m_lastFrameTickTime = 0;
    Logger::Instance().Info("View recentered");
}

void Mod::CycleTrackingMode() {
    switch (m_session.CycleMode()) {
        case TrackingMode::RotationAndPosition:
            Logger::Instance().Info("Tracking mode: full (rotation + position)");
            break;
        case TrackingMode::RotationOnly:
            Logger::Instance().Info("Tracking mode: rotation only (position disabled)");
            break;
        case TrackingMode::PositionOnly:
            Logger::Instance().Info("Tracking mode: position only (rotation disabled)");
            break;
    }
}

void Mod::TickFrame() {
    if (!m_initialized.load()) return;

    uint64_t now = cameraunlock::time::QpcNowMicros();
    float deltaTime = 0.016f;
    if (m_lastFrameTickTime > 0) {
        deltaTime = (now - m_lastFrameTickTime) / 1000000.0f;
        if (deltaTime > 0.1f) deltaTime = 0.1f;
        if (deltaTime < 0.0001f) deltaTime = 0.0001f;
    }
    m_lastFrameTickTime = now;
    m_lastDeltaTime = deltaTime;

    if (!m_session.Update(deltaTime)) return;

    if (m_diagFile) {
        const auto& raw = m_session.GetLastRaw();
        const auto& interpolated = m_session.GetLastInterpolated();
        const auto& processed = m_session.GetLastProcessed();

        double timeMs = (now - m_diagStartTime) / 1000.0;
        double deltMs = deltaTime * 1000.0;
        const char* marker = "";
        if (m_diagMarkerPending) {
            m_diagMarkerPending = false;
            m_diagMarkerCount++;
            marker = (m_diagMarkerCount == 1) ? "TOBII_END" : "APP_START";
        }
        fprintf(m_diagFile,
            "%.3f,%.3f,%.4f,%.4f,%.4f,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%s\n",
            timeMs, deltMs,
            raw.yaw, raw.pitch, raw.roll,
            m_session.WasNewSample() ? 1 : 0,
            interpolated.yaw, interpolated.pitch, interpolated.roll,
            processed.yaw, processed.pitch, processed.roll,
            marker);
        fflush(m_diagFile);
    }
}

bool Mod::GetProcessedRotation(float& yaw, float& pitch, float& roll) {
    return m_session.GetRotation(yaw, pitch, roll);
}

bool Mod::GetPositionOffset(float& x, float& y, float& z) {
    return m_session.GetPositionOffset(x, y, z);
}

void Mod::ToggleYawMode() {
    m_worldSpaceYaw = !m_worldSpaceYaw;
    Logger::Instance().Info("Yaw mode: %s", m_worldSpaceYaw ? "world-space (horizon-locked)" : "camera-local");
}

void Mod::PlaceDiagnosticMarker() {
    m_diagMarkerPending = true;
    Logger::Instance().Info("Diagnostic marker %d placed", m_diagMarkerCount + 1);
    // Also trigger game state diagnostic burst
    RE9HT::TriggerGameStateDiag();
}

void Mod::ToggleMarkersHidden() {
    bool now = !m_markersHidden.load();
    m_markersHidden.store(now);
    Logger::Instance().Info("World-anchored GUI markers: %s", now ? "HIDDEN" : "VISIBLE");
    // Re-arm the element dumper so the next few frames capture fresh state
    // (e.g. Visible=true while actually looking at an interactable).
    ResetGuiElementDumper();
}

void Mod::InitDiagnosticLog() {
    std::string diagPath = m_pluginDir + "HeadTracking_diag.csv";
    m_diagFile = fopen(diagPath.c_str(), "w");
    if (m_diagFile) {
        fprintf(m_diagFile,
            "time_ms,delta_ms,raw_yaw,raw_pitch,raw_roll,is_new_sample,"
            "interp_yaw,interp_pitch,interp_roll,proc_yaw,proc_pitch,proc_roll,marker\n");
        fflush(m_diagFile);
        m_diagStartTime = cameraunlock::time::QpcNowMicros();
        Logger::Instance().Info("Diagnostic log: %s", diagPath.c_str());
    }
}

} // namespace RE9HT
