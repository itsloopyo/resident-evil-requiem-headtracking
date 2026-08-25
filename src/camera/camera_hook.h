#pragma once

namespace RE9HT {

// Called from plugin_main's pre-BeginRendering callback
void OnPreBeginRendering();

// Called from plugin_main's post-BeginRendering callback — restores clean matrix
// so game logic (aim, raycasts, physics) never sees head-tracked state.
void OnPostBeginRendering();

// Where the shot lands, in normalised device coordinates of the drawn frame.
//
// Taken by pushing the impact world point through the camera's own view and
// projection matrices, rather than by decomposing the camera basis into
// tangents by hand. Every hand-derived version of this needed a separate
// decision about which matrix row is right and which is left, which way the
// canvas runs vertically, whether roll composes inside or outside the yaw and
// pitch, and whether the engine's FOV is the vertical or horizontal angle.
// None of those can be settled from the geometry alone - the answers depend on
// the engine's handedness convention, which is not observable from the camera
// matrix on its own - so each one shipped as a coin flip that could only be
// checked by a player firing at a wall. The view and projection matrices
// already encode all of it, and multiplying through them cannot disagree with
// what the GPU drew.
struct CrosshairProjection {
    float ndcX = 0.0f;
    float ndcY = 0.0f;
    float fovDegrees = 75.0f;
    float rollDegrees = 0.0f;
    // Normalised device coordinates per unit view tangent, from the projection
    // matrix ([0][0] and [1][1]). The marker path still works in tangents.
    float ndcPerTanX = 0.0f;
    float ndcPerTanY = 0.0f;
    bool valid = false;
};

const CrosshairProjection& GetCrosshairProjection();

// Rotation-only tangents for world-anchored GUI markers. They must NOT carry
// the reticle's lean term: a marker is at its own depth, and parallax is
// lean/depth, so the reticle's value is right for a marker sitting on the
// crosshair and wrong for every other one - badly wrong for a distant marker
// while the player aims at a near wall. A single container transform moves all
// the markers together and so cannot express a per-depth term at all.
//
// What this leaves uncorrected is the markers' own parallax, since the engine
// now projects them from the clean eye while the frame is drawn from the leaned
// one. That error is lean/depth, which fades with distance - and markers are
// mostly distant.
struct MarkerProjection {
    float tanRight = 0.0f;
    float tanUp = 0.0f;
    bool valid = false;
};

const MarkerProjection& GetMarkerProjection();

} // namespace RE9HT
