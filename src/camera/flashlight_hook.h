#pragma once

namespace RE9HT {

// Resolve the managed types and methods the flashlight scan needs. Called once
// alongside the other cached-method initialisation.
void InitFlashlightAccess();

// Rotate the player's flashlight by the head pose scaled by the configured
// multiplier. Called from the render phase, after the camera has been tracked.
void ApplyFlashlightTracking();

// Restore the orientations ApplyFlashlightTracking saved, so the game's
// flashlight sensor logic reads the direction the game itself aimed.
void RestoreFlashlightTracking();

} // namespace RE9HT
