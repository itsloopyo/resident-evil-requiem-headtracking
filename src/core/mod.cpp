#include "pch.h"
#include "mod.h"

#include <string>
#include "logger.h"
#include "camera/game_state_detector.h"
#include "camera/gui_compensation.h"

#include <cameraunlock/time/qpc_clock.h>

namespace RE9HT {

using cameraunlock::TrackingMode;

// The session re-reads the receiver's connection locality every Update() and
// selects LocalSmoothing or RemoteSmoothing from it, but that wiring is
// SFINAE-gated on the receiver exposing IsRemoteConnection(). A receiver
// adapter that failed to forward the method would still compile and would
// silently pin every connection to LocalSmoothing forever.
static_assert(cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver>::kHasRemoteConnection,
              "receiver must expose IsRemoteConnection() or remote smoothing never applies");

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

    // Determine plugin directory (used for config)
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

    Logger::Instance().Info("Smoothing: local=%.2f remote=%.2f",
                            m_config.localSmoothing, m_config.remoteSmoothing);

    m_session.SetMode(m_config.positionEnabled ? TrackingMode::RotationAndPosition
                                               : TrackingMode::RotationOnly);
    m_worldSpaceYaw = m_config.worldSpaceYaw;

    // Assigned by name rather than through the positional constructor.
    // PositionSettings takes nine floats before its three inversion bools, so a
    // positional call that gains or loses one argument silently rebinds a bool
    // to a float parameter - an invert flag would land in a smoothing slot - and
    // still compiles clean. Naming every field removes that failure mode.
    cameraunlock::PositionSettings posSettings;
    posSettings.sensitivity_x = m_config.positionSensitivityX;
    posSettings.sensitivity_y = m_config.positionSensitivityY;
    posSettings.sensitivity_z = m_config.positionSensitivityZ;
    posSettings.limit_x = m_config.positionLimitX;
    posSettings.limit_y = m_config.positionLimitY;
    // Asymmetric Z: negative z is the forward lean, so the generous limit_z is
    // the forward range and limit_z_back restricts leaning back into the player.
    posSettings.limit_z = m_config.positionLimitZ;
    posSettings.limit_z_back = m_config.positionLimitZBack;
    // Position smoothing lives on the settings; the processor picks between the
    // two per connection from the flag the session feeds it.
    posSettings.local_smoothing = m_config.localSmoothing;
    posSettings.remote_smoothing = m_config.remoteSmoothing;
    // Protocol-to-engine axis conversion, fixed here rather than exposed.
    //
    // The tracker owns pose shaping; a mod converts conventions once, at the
    // boundary, and a user must not be able to undo it. These were INI knobs,
    // and the lateral lean was only coming out the right way because InvertX
    // defaulting to true happened to cancel the negation the camera boundary
    // applies. One INI edit reversed the lean, and a reversed lean reads as
    // working until the reticle has to agree with it.
    // false, so that the single negation the shared helper applies is the whole
    // conversion. Setting it true cancelled that helper and left the lateral
    // lean un-mirrored, which is what the fleet's 27 other mods do NOT do.
    // Measured: a positive processed x displaced the camera along minus the
    // camera's lateral axis, and in game the lean came out reversed.
    posSettings.invert_x = false;
    posSettings.invert_y = false;
    posSettings.invert_z = false;
    m_session.GetPositionProcessor().SetSettings(posSettings);

    // Rotation smoothing. The session setter also re-writes the two values into
    // the position settings above, so it has to run after SetSettings; the
    // values are identical either way, which keeps rotation and position from
    // ever drifting apart.
    m_session.SetLocalSmoothing(m_config.localSmoothing);
    m_session.SetRemoteSmoothing(m_config.remoteSmoothing);

    // The previous per-mod pipeline never engaged tracker pivot compensation
    // (it passed radians to a degrees API, zeroing the artifact). Keep that
    // tuning until pivot compensation is verified in game.
    m_session.GetPositionProcessor().SetTrackerPivotForward(0.0f);

    Logger::Instance().Info("Position: %s, sens=%.1f/%.1f/%.1f",
                            IsPositionEnabled() ? "6DOF" : "3DOF",
                            posSettings.sensitivity_x, posSettings.sensitivity_y, posSettings.sensitivity_z);

    // Forwarded so the core receiver's bind result, retry progress and latched
    // first-packet line reach the log. Set before Start so the bind is captured.
    m_udpReceiver.SetLog([](const std::string& msg) {
        Logger::Instance().Info("UDP: %s", msg.c_str());
    });

    // A busy port is not fatal: the core receiver's supervisor thread keeps
    // retrying, so tracking comes up on its own once the port frees. Returning
    // false here aborted plugin init before the render callback was registered,
    // so the camera never ran and nothing could report the recovery.
    if (m_udpReceiver.Start(m_config.udpPort)) {
        Logger::Instance().Info("UDP receiver started on port %d", m_config.udpPort);
    } else {
        Logger::Instance().Warning("UDP port %d busy - retrying in background", m_config.udpPort);
    }

    if (m_config.autoEnable) {
        m_enabled.store(true);
        Logger::Instance().Info("Head tracking auto-enabled");
    }

    m_initialized.store(true);
    Logger::Instance().Info("Initialization complete");
    return true;
}

void Mod::Shutdown() {
    if (!m_initialized.load()) return;

    Logger::Instance().Info("Shutting down...");
    m_udpReceiver.Stop();
    m_initialized.store(false);
    Logger::Instance().Info("Shutdown complete");
}

bool Mod::LoadConfig() {
    std::string configPath = m_pluginDir + "HeadTracking.ini";

    if (!m_config.Load(configPath.c_str())) {
        m_config.SetDefaults();
        m_config.Save(configPath.c_str());
        Logger::Instance().Warning("Config not found at %s - defaults written there", configPath.c_str());
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
}

void Mod::LogFirstTrackerPose() {
    if (!m_initialized.load()) return;
    if (m_loggedFirstPose) return;

    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    if (!m_udpReceiver.GetRotation(yaw, pitch, roll)) return;

    m_loggedFirstPose = true;
    Logger::Instance().Info("First tracker pose received: yaw=%.2f pitch=%.2f roll=%.2f (%s connection)",
                            yaw, pitch, roll,
                            m_udpReceiver.IsRemoteConnection() ? "remote" : "local");
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

} // namespace RE9HT
