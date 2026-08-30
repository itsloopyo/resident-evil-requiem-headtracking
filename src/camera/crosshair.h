#pragma once

#include <cameraunlock/reframework/re_math.h>

namespace RE9HT {

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
    // Normalised device coordinates per unit view tangent. Vertical is the
    // projection matrix's [1][1]; horizontal is derived from it under the
    // square-pixel guard rather than read from [0][0] - see crosshair.cpp. The
    // marker path still works in tangents.
    float ndcPerTanX = 0.0f;
    float ndcPerTanY = 0.0f;
    bool valid = false;
};

const CrosshairProjection& GetCrosshairProjection();

// Resolve the projection-matrix getter and the game-specific subsystems.
// Called once from the camera pipeline's init hook.
void InitCrosshairProjection();

// Update the crosshair projection and drive the flashlight. Called from the
// camera pipeline once head tracking has been applied for the frame.
void OnFrameApplied(const cameraunlock::reframework::Matrix4x4f& clean,
                    const cameraunlock::reframework::Matrix4x4f& head);

// Put the flashlight beam back before the pipeline restores the clean camera.
void OnPostRestore();

} // namespace RE9HT
