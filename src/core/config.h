#pragma once

#include <cameraunlock/reframework/plugin_config.h>

namespace RE9HT {

using Config = cameraunlock::reframework::PluginConfig;

// Requiem's INI schema: the [Flashlight] section, and no [Position] Invert
// keys. The axis conversion is fixed at the camera boundary here - the tracker
// owns pose shaping, a mod converts conventions once, and a user must not be
// able to undo it. The keys were removed after one INI edit reversed the
// lateral lean, which reads as working until the reticle has to agree with it.
inline constexpr cameraunlock::reframework::PluginConfigSchema kConfigSchema{
    /*title*/ "RE9 Head Tracking",
    /*positionInvertKeys*/ false,
    /*flashlight*/ true,
    /*diagnosticMarkerKey*/ false,
    /*positionSensitivity*/ 1.0f,
};

} // namespace RE9HT
