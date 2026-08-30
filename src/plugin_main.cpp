#include "pch.h"

#include <reframework/API.hpp>

#include "camera/crosshair.h"
#include "camera/game_state_detector.h"
#include "camera/gui_compensation.h"
#include "core/config.h"

#include <cameraunlock/reframework/gameplay_gate.h>
#include <cameraunlock/reframework/plugin_bootstrap.h>

namespace ref = cameraunlock::reframework;

namespace {

const char* const kControllerCandidateTypes[] = {
    "requiem.PlayerCameraController",
    "requiem.camera.PlayerCameraController",
    "app.PlayerCameraController",
    "app.camera.PlayerCameraController",
};

// Requiem places its reticle from the projection matrix rather than from the
// shared aim tangents, and measures the aim range with a physics cast rather
// than assuming one, so the pipeline's aim projection is left off and the
// crosshair hook owns it.
const ref::PluginBootstrapDescriptor kPlugin = [] {
    ref::PluginBootstrapDescriptor d;
    d.logTag = "RE9HT";
    d.mod.displayName = RE9HT::RE9HT_PLUGIN_NAME;
    d.mod.version = RE9HT::RE9HT_VERSION;
    d.mod.config = RE9HT::kConfigSchema;
    d.camera.controllerCandidateTypes = kControllerCandidateTypes;
    d.camera.controllerCandidateCount =
        static_cast<int>(std::size(kControllerCandidateTypes));
    // The player camera controller has not matched a type on this game in any
    // captured log, and the hooker's parent-chain walk logs every component it
    // sees on each attempt. Retrying on a cooldown bounds that logging without
    // capping discovery, so a rig rebuilt late in a session is still caught.
    d.camera.hookRetryCooldownFrames = 120;
    d.camera.gate = RE9HT::GameplayGateInstance();
    d.camera.onInit = &RE9HT::InitCrosshairProjection;
    d.camera.onFrameApplied = &RE9HT::OnFrameApplied;
    d.camera.onPostRestore = &RE9HT::OnPostRestore;
    d.preGuiDrawElement = &RE9HT::OnPreGuiDrawElement;
    d.centerGameWindow = true;
    return d;
}();

} // namespace

// --- REFramework plugin exports ---

extern "C" __declspec(dllexport)
void reframework_plugin_required_version(REFrameworkPluginVersion* version) {
    version->major = REFRAMEWORK_PLUGIN_VERSION_MAJOR;
    version->minor = REFRAMEWORK_PLUGIN_VERSION_MINOR;
    version->patch = REFRAMEWORK_PLUGIN_VERSION_PATCH;
    version->game_name = nullptr;
}

extern "C" __declspec(dllexport)
bool reframework_plugin_initialize(const REFrameworkPluginInitializeParam* param) {
    if (!param) return false;
    return ref::InitializePlugin(param, kPlugin);
}
