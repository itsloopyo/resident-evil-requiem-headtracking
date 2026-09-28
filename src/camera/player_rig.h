#pragma once

#include <reframework/API.hpp>

namespace RE9HT {

void InitPlayerRig();

// app.PlayerCameraPositionSetting._IsHold on the player: the game's own "a
// weapon is raised" state, which its camera setting switches on. False when the
// player cannot be read.
bool IsAiming();

// The player root, cp_A100, is the rig: the camera is placed from it, the arms
// and the held weapon are its children, and the shot starts at the camera.
bool RigAvailable();
bool WriteRig(const float worldOffset[3]);
void RestoreRig();

// The player's via.physics.CharacterController filter, or null.
reframework::API::ManagedObject* PlayerCollisionFilter();

// The field of view the game renders with no weapon raised, taken from the live
// camera once the aim has been down long enough for the zoom out to finish.
// Requiem has no field of view option, so this is the game's own un-zoomed view.
bool UnzoomedFovDegrees(float& out);

} // namespace RE9HT
