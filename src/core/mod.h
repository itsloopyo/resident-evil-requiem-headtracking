#pragma once

#include "config.h"
#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/processing/tracking_processor.h>
#include <cameraunlock/processing/pose_interpolator.h>
#include <cameraunlock/processing/position_processor.h>
#include <cameraunlock/processing/position_interpolator.h>
#include <cstdio>
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

    void Recenter();
    void TogglePosition();
    void ToggleYawMode();
    void PlaceDiagnosticMarker();
    void ToggleMarkersHidden();

    Config& GetConfig() { return m_config; }
    const Config& GetConfig() const { return m_config; }

    bool GetProcessedRotation(float& yaw, float& pitch, float& roll);
    bool GetPositionOffset(float& x, float& y, float& z);
    bool IsPositionEnabled() const { return m_positionEnabled; }
    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw; }
    float GetLastDeltaTime() const { return m_lastDeltaTime; }
    bool AreMarkersHidden() const { return m_markersHidden.load(); }

    Mod(const Mod&) = delete;
    Mod& operator=(const Mod&) = delete;

private:
    Mod() = default;
    ~Mod() = default;

    bool LoadConfig();
    void InitDiagnosticLog();

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_markersHidden{false};

    Config m_config;
    cameraunlock::UdpReceiver m_udpReceiver;
    cameraunlock::PoseInterpolator m_poseInterpolator;
    cameraunlock::TrackingProcessor m_processor;
    int64_t m_lastReceiveTimestamp = 0;

    cameraunlock::PositionProcessor m_positionProcessor;
    cameraunlock::PositionInterpolator m_positionInterpolator;
    bool m_positionEnabled = true;
    bool m_worldSpaceYaw = false;

    uint64_t m_lastProcessTime = 0;
    float m_lastDeltaTime = 0.016f;

    float m_cachedYaw = 0.0f;
    float m_cachedPitch = 0.0f;
    float m_cachedRoll = 0.0f;
    bool m_cachedValid = false;
    bool m_hasCentered = false;
    int m_stabilizationFrames = 0;

    // Previous raw values for new-sample detection (data change, not just packet arrival)
    float m_lastRawYaw = 0.0f;
    float m_lastRawPitch = 0.0f;
    float m_lastRawRoll = 0.0f;

    // Diagnostic logging
    std::string m_pluginDir;
    FILE* m_diagFile = nullptr;
    uint64_t m_diagStartTime = 0;
    bool m_diagMarkerPending = false;
    int m_diagMarkerCount = 0;
};

} // namespace RE9HT
