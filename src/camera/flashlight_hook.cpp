#include "pch.h"
#include "flashlight_hook.h"
#include "camera_internal.h"
#include "core/mod.h"
#include "core/logger.h"

#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/reframework/re_math.h>

#include <reframework/API.hpp>
#include <string>

namespace RE9HT {

namespace ref = cameraunlock::reframework;

// The beam is a POOLED light. It is not a component on the flashlight rig and
// never appears under it: a walk of all 84 nodes under PlayerFlashLightController
// turns up a mesh, a rigid body and script updaters, and not one via.render
// light. app.FlashLightController checks a light out of a pool and parks the
// result in _CurrentLightObject, so that field is the only reliable handle on
// the object actually emitting the beam.
//
// This is also why nothing found by proximity could ever have been right. The
// scene carries over three thousand spot lights, the pooled beam is not
// meaningfully nearer the camera than the level's own rigs, and the level rigs
// sit within a metre of the player routinely.
static const char* const kControllerTypeShortName = "FlashLightController";
static const char* const kCurrentLightObjectField = "_CurrentLightObject";

// Re-resolve cadence for the controller itself. Loading a save tears the player
// down and builds a new one, so a controller resolved once and pinned forever
// is left pointing at the dead scene's component, whose _CurrentLightObject
// never fills again. The scene is asked again on this cadence whether or not a
// controller is already cached, which is what makes the beam come back.
constexpr int kControllerResolveIntervalFrames = 120;

static struct {
    reframework::API::Method* getCurrentScene = nullptr;
    reframework::API::Method* findComponents = nullptr;
    reframework::API::Method* getTransform = nullptr;
    reframework::API::Field* currentLightObject = nullptr;
    reframework::API::ManagedObject* controllerType = nullptr;
    bool initialized = false;
    bool failed = false;
} g_access;

// The controller and the beam transform are held by reference for as long as
// they are cached, so a light returned to the pool between frames cannot leave
// this writing through freed memory.
static reframework::API::ManagedObject* g_controller = nullptr;
static reframework::API::ManagedObject* g_lightObject = nullptr;
static reframework::API::ManagedObject* g_lightTransform = nullptr;
static Matrix4x4f g_savedLightMatrix;
static bool g_appliedThisFrame = false;
static int g_frameCounter = 0;
static int g_nextControllerResolveFrame = 0;
static int g_beamLogsLeft = 4;

static Matrix4x4f* WorldMatrix(reframework::API::ManagedObject* transform) {
    return reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + ref::kTransformWorldMatrixOffset);
}

// --- Initialisation ---

void InitFlashlightAccess() {
    if (g_access.initialized) return;
    g_access.initialized = true;

    const auto& api = reframework::API::get();
    auto tdb = api->tdb();

    auto smType = tdb->find_type("via.SceneManager");
    auto goType = tdb->find_type("via.GameObject");

    g_access.getCurrentScene = smType ? smType->find_method("get_CurrentScene") : nullptr;
    g_access.getTransform = goType ? goType->find_method("get_Transform") : nullptr;
    g_access.findComponents = ref::FindMethodByParamTypeName("via.Scene", "findComponents", "Type");

    // Short name, then full name: RE Engine moves types between namespaces
    // across titles, so the namespace is read off the TDB rather than assumed.
    std::string controllerFullName;
    for (auto* td : ref::FindTypesByShortName(kControllerTypeShortName)) {
        auto runtimeType = api->typeof(td->get_full_name().c_str());
        if (!runtimeType) continue;
        g_access.controllerType = runtimeType;
        g_access.currentLightObject = td->find_field(kCurrentLightObjectField);
        controllerFullName = td->get_full_name();
        break;
    }

    g_access.failed = !g_access.getCurrentScene || !g_access.findComponents
        || !g_access.getTransform || !g_access.controllerType || !g_access.currentLightObject;

    Logger::Instance().Info("Flashlight access: type=%s getCurrentScene=%p findComponents=%p "
        "getTransform=%p %s=%p%s",
        controllerFullName.empty() ? kControllerTypeShortName : controllerFullName.c_str(),
        (void*)g_access.getCurrentScene, (void*)g_access.findComponents,
        (void*)g_access.getTransform, kCurrentLightObjectField,
        (void*)g_access.currentLightObject,
        g_access.failed ? " - FLASHLIGHT TRACKING UNAVAILABLE" : "");
}

// --- Resolution ---

static void ReleaseControllerCache() {
    if (g_controller) g_controller->release();
    g_controller = nullptr;
}

static void ReleaseLightCache() {
    if (g_lightTransform) g_lightTransform->release();
    g_lightTransform = nullptr;
    g_lightObject = nullptr;
}

static void ResolveController() {
    if (g_frameCounter < g_nextControllerResolveFrame) return;
    g_nextControllerResolveFrame = g_frameCounter + kControllerResolveIntervalFrames;

    auto sm = reframework::API::get()->get_native_singleton("via.SceneManager");
    if (!sm) return;

    auto scene = reinterpret_cast<reframework::API::ManagedObject*>(
        ref::CallMethod(g_access.getCurrentScene, sm));
    if (!scene) return;

    auto arrPtr = ref::CallMethodArg(g_access.findComponents, scene, g_access.controllerType);
    if (!arrPtr) return;

    auto arr = reinterpret_cast<reframework::API::ManagedObject*>(arrPtr);
    auto lenRet = arr->invoke("get_Length", ref::EmptyArgs());
    if (lenRet.exception_thrown || lenRet.dword == 0) return;

    auto controller = ref::ArrayGetValue(arr, 0);
    if (!controller || controller == g_controller) return;

    // The beam cache is keyed on the light object's address, so it has to go
    // with the controller: a light checked out by the new scene can land on the
    // address the old one had and would otherwise be taken for the cached one.
    ReleaseLightCache();
    ReleaseControllerCache();

    controller->add_ref();
    g_controller = controller;
    g_beamLogsLeft = 4;
    Logger::Instance().Info("Flashlight: controller resolved (%u instance(s))", lenRet.dword);
}

// Read the pooled beam's GameObject straight off the controller. Done every
// frame rather than cached on an interval: the field is a pointer read at a
// known offset, and it is the game's own statement of which light is currently
// checked out, so a torch stowed or swapped is picked up on the next frame
// instead of after a timer.
static reframework::API::ManagedObject* ReadCurrentLightObject() {
    __try {
        return g_access.currentLightObject->get_data<reframework::API::ManagedObject*>(g_controller);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static void ResolveLightTransform() {
    auto lightObject = ReadCurrentLightObject();

    if (!lightObject) {
        ReleaseLightCache();
        return;
    }
    if (lightObject == g_lightObject) return;

    ReleaseLightCache();

    auto transform = reinterpret_cast<reframework::API::ManagedObject*>(
        ref::CallMethod(g_access.getTransform, lightObject));
    if (!transform) return;

    transform->add_ref();
    g_lightObject = lightObject;
    g_lightTransform = transform;

    if (g_beamLogsLeft > 0) {
        g_beamLogsLeft--;
        Logger::Instance().Info("Flashlight: beam light object %p -> transform %p",
            (void*)lightObject, (void*)transform);
    }
}

// --- Per-frame application ---

static void RotateLight(float yawRad, float pitchRad, float rollRad, bool worldSpaceYaw) {
    __try {
        Matrix4x4f* m = WorldMatrix(g_lightTransform);
        g_savedLightMatrix = *m;
        if (worldSpaceYaw) {
            ref::ApplyWorldSpaceHeadRotation(*m, yawRad, pitchRad, rollRad);
        } else {
            ref::ApplyCameraLocalHeadRotation(*m, yawRad, pitchRad, rollRad);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void RestoreLight() {
    __try {
        *WorldMatrix(g_lightTransform) = g_savedLightMatrix;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void ApplyFlashlightTracking() {
    const Config& config = Mod::Instance().GetConfig();
    if (!config.flashlightTracking || g_access.failed) return;

    float yaw = 0.f, pitch = 0.f, roll = 0.f;
    if (!Mod::Instance().GetProcessedRotation(yaw, pitch, roll)) return;

    g_frameCounter++;
    ResolveController();
    if (!g_controller) return;

    ResolveLightTransform();
    if (!g_lightTransform) return;

    // The same composition, the same signs and the same yaw mode the camera
    // gets, scaled by the multiplier, so the beam can never disagree with the
    // view about which way the head turned. Rotating the light about its own
    // basis is what makes the beam lead the view rather than orbit it.
    const float k = config.flashlightMultiplier;
    RotateLight(-yaw * DEG_TO_RAD * k, pitch * DEG_TO_RAD * k, roll * DEG_TO_RAD * k,
                Mod::Instance().IsWorldSpaceYaw());
    g_appliedThisFrame = true;
}

void RestoreFlashlightTracking() {
    if (!g_appliedThisFrame) return;
    g_appliedThisFrame = false;
    if (!g_lightTransform) return;

    RestoreLight();
}

} // namespace RE9HT
