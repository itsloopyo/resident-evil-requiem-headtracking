#pragma once

namespace RE9HT {

// Called from plugin_main's pre-BeginRendering callback
void OnPreBeginRendering();

// Called from plugin_main's post-BeginRendering callback — restores clean matrix
// so game logic (aim, raycasts, physics) never sees head-tracked state.
void OnPostBeginRendering();

// Crosshair projection state (read by crosshair overlay and GUI compensation)
struct CrosshairProjection {
    float tanRight = 0.0f;
    float tanUp = 0.0f;
    float fovDegrees = 75.0f;
    float rollDegrees = 0.0f;
    bool valid = false;
};

const CrosshairProjection& GetCrosshairProjection();

// Marker projection — same shape as the crosshair projection but computed at
// a smaller assumed depth so translation parallax has the right magnitude
// for typical world-anchored UI markers (interaction prompts, objective
// icons). Rotation is depth-independent so the angular shift matches the
// crosshair; only the position-offset contribution differs.
struct MarkerProjection {
    float tanRight = 0.0f;
    float tanUp = 0.0f;
    bool valid = false;
};

const MarkerProjection& GetMarkerProjection();

} // namespace RE9HT
