#pragma once

namespace RE9HT {

// Called from plugin_main's pre-BeginRendering callback
void OnPreBeginRendering();

// Called from plugin_main's post-BeginRendering callback — restores clean matrix
// so game logic (aim, raycasts, physics) never sees head-tracked state.
void OnPostBeginRendering();

// Called from game_state_detector when transitioning from gameplay to
// non-gameplay (cinematic, loading, menu). Clears per-frame caches that
// could otherwise leak stale state into the transition.
void OnLeftGameplay();

// Crosshair projection state (read by crosshair overlay and GUI compensation)
struct CrosshairProjection {
    float tanRight = 0.0f;
    float tanUp = 0.0f;
    float fovDegrees = 75.0f;
    float rollDegrees = 0.0f;
    bool valid = false;
};

const CrosshairProjection& GetCrosshairProjection();

} // namespace RE9HT
