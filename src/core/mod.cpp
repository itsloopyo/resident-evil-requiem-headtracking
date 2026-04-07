#include "pch.h"
#include "mod.h"
#include "logger.h"
#include "camera/game_state_detector.h"
#include "camera/gui_compensation.h"

#include <algorithm>
#include <cameraunlock/math/smoothing_utils.h>

namespace RE9HT {

// Skip noisy initial frames before auto-recentering (~0.5s at 60fps)
constexpr int STABILIZATION_FRAME_COUNT = 30;
// Avoid re-processing rotation within the same frame (microseconds)
constexpr uint64_t ROTATION_CACHE_THRESHOLD_US = 1000;

static uint64_t GetTimeMicros() {
    static double microsPerTick = 0.0;
    if (microsPerTick == 0.0) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        microsPerTick = 1000000.0 / static_cast<double>(freq.QuadPart);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<uint64_t>(static_cast<double>(now.QuadPart) * microsPerTick);
}

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

    // Initialize TrackingProcessor
    cameraunlock::SensitivitySettings sensitivity;
    sensitivity.yaw = m_config.yawMultiplier;
    sensitivity.pitch = m_config.pitchMultiplier;
    sensitivity.roll = m_config.rollMultiplier;
    m_processor.SetSensitivity(sensitivity);

    Logger::Instance().Info("Sensitivity: yaw=%.2f pitch=%.2f roll=%.2f",
                            sensitivity.yaw, sensitivity.pitch, sensitivity.roll);

    // Initialize position processor
    m_positionEnabled = m_config.positionEnabled;
    m_worldSpaceYaw = m_config.worldSpaceYaw;

    cameraunlock::PositionSettings posSettings(
        m_config.positionSensitivityX, m_config.positionSensitivityY, m_config.positionSensitivityZ,
        m_config.positionLimitX, m_config.positionLimitY, m_config.positionLimitZ, m_config.positionLimitZBack,
        m_config.positionSmoothing,
        m_config.positionInvertX, m_config.positionInvertY, m_config.positionInvertZ
    );
    m_positionProcessor.SetSettings(posSettings);

    Logger::Instance().Info("Position: %s, sens=%.1f/%.1f/%.1f",
                            m_positionEnabled ? "6DOF" : "3DOF",
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
    m_udpReceiver.Recenter();
    m_processor.Reset();
    m_poseInterpolator.Reset();
    m_lastProcessTime = 0;

    float px, py, pz;
    if (m_udpReceiver.GetPosition(px, py, pz)) {
        cameraunlock::PositionData posCenter(px, py, pz);
        m_positionProcessor.SetCenter(posCenter);
    }
    m_positionInterpolator.Reset();

    Logger::Instance().Info("View recentered");
}

void Mod::TogglePosition() {
    m_positionEnabled = !m_positionEnabled;
    if (!m_positionEnabled) {
        m_positionProcessor.Reset();
        m_positionInterpolator.Reset();
    }
    Logger::Instance().Info("Position tracking %s", m_positionEnabled ? "enabled" : "disabled");
}

bool Mod::GetProcessedRotation(float& yaw, float& pitch, float& roll) {
    uint64_t now = GetTimeMicros();
    if (m_lastProcessTime > 0 && (now - m_lastProcessTime) < ROTATION_CACHE_THRESHOLD_US) {
        yaw = m_cachedYaw;
        pitch = m_cachedPitch;
        roll = m_cachedRoll;
        return m_cachedValid;
    }

    float rawYaw, rawPitch, rawRoll;
    if (!m_udpReceiver.GetRotation(rawYaw, rawPitch, rawRoll)) {
        m_lastProcessTime = now;
        m_cachedValid = false;
        return false;
    }

    // Wait for stabilization before auto-recentering (skip noisy initial frames)
    if (!m_hasCentered) {
        m_stabilizationFrames++;
        if (m_stabilizationFrames >= STABILIZATION_FRAME_COUNT) {
            m_hasCentered = true;
            Recenter();
            Logger::Instance().Info("Auto-recentered after %d frames", m_stabilizationFrames);
        }
        // Still process data below so smoothing settles
    }

    float deltaTime = 0.016f;
    if (m_lastProcessTime > 0) {
        deltaTime = (now - m_lastProcessTime) / 1000000.0f;
        if (deltaTime > 0.1f) deltaTime = 0.1f;
        if (deltaTime < 0.0001f) deltaTime = 0.0001f;
    }
    m_lastProcessTime = now;
    m_lastDeltaTime = deltaTime;

    int64_t receiveTs = m_udpReceiver.GetLastReceiveTimestamp();
    bool isNewPacket = (receiveTs != m_lastReceiveTimestamp);
    m_lastReceiveTimestamp = receiveTs;

    // Detect new DATA, not just new packets. If a tracker sends at a higher rate
    // than its sensor updates (e.g. phone app at 60Hz with 30Hz IMU), duplicate
    // packets would fool the interpolator into thinking samples arrive at 60Hz,
    // preventing it from generating smooth inter-sample frames.
    bool isNewSample = isNewPacket &&
        (rawYaw != m_lastRawYaw || rawPitch != m_lastRawPitch || rawRoll != m_lastRawRoll);
    if (isNewPacket) {
        m_lastRawYaw = rawYaw;
        m_lastRawPitch = rawPitch;
        m_lastRawRoll = rawRoll;
    }

    cameraunlock::InterpolatedPose interpolated = m_poseInterpolator.Update(
        rawYaw, rawPitch, rawRoll, isNewSample, deltaTime);

    cameraunlock::TrackingPose processed = m_processor.Process(
        interpolated.yaw, interpolated.pitch, interpolated.roll, deltaTime);

    yaw = processed.yaw;
    pitch = processed.pitch;
    roll = processed.roll;

    m_cachedYaw = yaw;
    m_cachedPitch = pitch;
    m_cachedRoll = roll;
    m_cachedValid = true;

    // Diagnostic CSV logging
    if (m_diagFile) {
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
            rawYaw, rawPitch, rawRoll,
            isNewSample ? 1 : 0,
            interpolated.yaw, interpolated.pitch, interpolated.roll,
            processed.yaw, processed.pitch, processed.roll,
            marker);
        fflush(m_diagFile);
    }

    return true;
}

bool Mod::GetPositionOffset(float& x, float& y, float& z) {
    if (!m_positionEnabled) {
        x = y = z = 0.0f;
        return false;
    }

    float rawX, rawY, rawZ;
    if (!m_udpReceiver.GetPosition(rawX, rawY, rawZ)) {
        x = y = z = 0.0f;
        return false;
    }

    float deltaTime = m_lastDeltaTime;
    // Use the UDP receiver's packet timestamp so the interpolator can distinguish
    // new samples from stale reads. The 3-arg PositionData constructor uses
    // CurrentTimestamp() which is unique every frame — defeating new-sample detection.
    int64_t receiveTs = m_udpReceiver.GetLastReceiveTimestamp();
    cameraunlock::PositionData rawPos(rawX, rawY, rawZ, receiveTs);
    cameraunlock::PositionData interpolatedPos = m_positionInterpolator.Update(rawPos, deltaTime);

    cameraunlock::math::Quat4 headRotQ = cameraunlock::math::Quat4::FromYawPitchRoll(
        m_cachedYaw * static_cast<float>(cameraunlock::math::kDegToRad),
        m_cachedPitch * static_cast<float>(cameraunlock::math::kDegToRad),
        m_cachedRoll * static_cast<float>(cameraunlock::math::kDegToRad));

    cameraunlock::math::Vec3 offset = m_positionProcessor.Process(interpolatedPos, headRotQ, deltaTime);

    x = offset.x;
    y = offset.y;
    z = offset.z;
    return true;
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
        m_diagStartTime = GetTimeMicros();
        Logger::Instance().Info("Diagnostic log: %s", diagPath.c_str());
    }
}

} // namespace RE9HT
