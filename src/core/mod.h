#pragma once

#include "config.h"
#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/tracking/head_tracking_session.h>
#include <string>

namespace RE9HT {

class Mod {
public:
    static Mod& Instance();

    bool Initialize();
    void Shutdown();

    bool IsEnabled() const { return m_enabled.load(); }
    void SetEnabled(bool enabled);
    void Toggle();

    void CycleTrackingMode();
    void ToggleYawMode();

    Config& GetConfig() { return m_config; }
    const Config& GetConfig() const { return m_config; }

    // Advance interpolation + smoothing pipelines once per render frame.
    // Caches the smoothed rotation and position so every in-frame consumer
    // (camera matrix, crosshair projection, GUI marker compensation) reads
    // an identical value. Without this, per-element GUI calls would each
    // re-tick the pipeline with a fragmented dt, leaving the rendered
    // camera advancing on a partial-frame dt while position smoothing sees
    // an even smaller one.
    void TickFrame();

    // Latches the first tracker packet. Called from an ungated point in the
    // render callback: the answer to "did the tracker ever send anything"
    // must not depend on tracking being enabled or the camera hook engaging.
    void LogFirstTrackerPose();

    bool GetProcessedRotation(float& yaw, float& pitch, float& roll);
    bool GetPositionOffset(float& x, float& y, float& z);
    bool IsPositionEnabled() const { return m_session.IsPositionActive(); }
    bool IsRotationEnabled() const { return m_session.IsRotationActive(); }
    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw; }
    float GetLastDeltaTime() const { return m_lastDeltaTime; }

    Mod(const Mod&) = delete;
    Mod& operator=(const Mod&) = delete;

private:
    Mod() = default;
    ~Mod() = default;

    bool LoadConfig();

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_initialized{false};

    Config m_config;
    cameraunlock::UdpReceiver m_udpReceiver;
    cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver> m_session{m_udpReceiver};
    bool m_worldSpaceYaw = false;

    bool m_loggedFirstPose = false;

    uint64_t m_lastFrameTickTime = 0;
    float m_lastDeltaTime = 0.016f;

    std::string m_pluginDir;
};

} // namespace RE9HT
