#pragma once

#include <cameraunlock/reframework/plugin_config.h>

namespace RE9HT {

using Config = cameraunlock::reframework::PluginConfig;

// The game's name as cameraunlock-core's data/games.json spells it, written at
// the top of CameraUnlock.ini.
inline constexpr const char* kGameName = "Resident Evil Requiem";

// Requiem's INI schema: the [Flashlight] section, and no [Position] Invert
// keys. The axis conversion is fixed at the camera boundary here - the tracker
// owns pose shaping, a mod converts conventions once, and a user must not be
// able to undo it. The keys were removed after one INI edit reversed the
// lateral lean, which reads as working until the reticle has to agree with it.
//
// canonicalConfig: settings live in reframework\plugins\CameraUnlock.ini, and
// HeadTracking.ini, the file every earlier release read, is imported once while
// CameraUnlock.ini is absent and never written.
inline constexpr cameraunlock::reframework::PluginConfigSchema kConfigSchema{
    /*title*/ "RE9 Head Tracking",
    /*positionInvertKeys*/ false,
    /*flashlight*/ true,
    /*diagnosticMarkerKey*/ false,
    /*positionSensitivity*/ 1.0f,
    /*modId*/ "re9",
    /*canonicalConfig*/ true,
    /*trueFreeLook*/ true,
    /*leanCollision*/ true,
};

} // namespace RE9HT
