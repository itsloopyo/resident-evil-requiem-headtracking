#include "pch.h"
#include "player_rig.h"

#include <cameraunlock/reframework/camera_pipeline.h>
#include <cameraunlock/reframework/log_callback.h>
#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/time/qpc_clock.h>

#include <reframework/API.hpp>

namespace RE9HT {

namespace ref = cameraunlock::reframework;

// Found on the player root, cp_A100. The component the camera places itself
// with, and the one that knows whether a weapon is raised.
static const char* const kPositionSettingType = "app.PlayerCameraPositionSetting";
static const char* const kIsHoldField = "_IsHold";

// Loading a save tears the player down and builds a new one, so the component is
// looked up again on this cadence whether or not one is cached.
constexpr uint64_t kResolveIntervalFrames = 120;

// How long the aim has to have been down before the live field of view is taken
// as the un-zoomed one. Lowering the weapon zooms back out over about a third of
// a second, and a view caught part way out would be taken for the hip.
constexpr uint64_t kHipSettleUs = 500000;

static struct {
    reframework::API::Method* getCurrentScene = nullptr;
    reframework::API::Method* findComponents = nullptr;
    reframework::API::Method* getGameObject = nullptr;
    reframework::API::Method* getTransform = nullptr;
    reframework::API::Method* getPosition = nullptr;
    reframework::API::Method* setPosition = nullptr;
    reframework::API::Field* isHold = nullptr;
    reframework::API::ManagedObject* settingType = nullptr;
    reframework::API::Method* getComponent = nullptr;
    reframework::API::Method* getFilterInfo = nullptr;
    reframework::API::ManagedObject* controllerType = nullptr;
    bool failed = true;
} g_access;

// Held by reference for as long as they are cached.
static reframework::API::ManagedObject* g_setting = nullptr;
static reframework::API::ManagedObject* g_root = nullptr;
static reframework::API::ManagedObject* g_filter = nullptr;
static uint64_t g_nextResolveFrame = 0;

static struct {
    float offset[3] = {};
    bool written = false;
} g_rig;

static struct {
    float degrees = 0.f;
    bool has = false;
    uint64_t hipSinceUs = 0;
} g_fov;

void InitPlayerRig() {
    const auto& api = reframework::API::get();
    auto tdb = api->tdb();

    auto smType = tdb->find_type("via.SceneManager");
    auto componentType = tdb->find_type("via.Component");
    auto goType = tdb->find_type("via.GameObject");
    auto settingTd = tdb->find_type(kPositionSettingType);

    g_access.getCurrentScene = smType ? smType->find_method("get_CurrentScene") : nullptr;
    g_access.findComponents = ref::FindMethodByParamTypeName("via.Scene", "findComponents", "Type");
    g_access.getGameObject = componentType ? componentType->find_method("get_GameObject") : nullptr;
    g_access.getTransform = goType ? goType->find_method("get_Transform") : nullptr;
    g_access.getPosition = ref::FindMethodByParamCount("via.Transform", "get_Position", 0);
    g_access.setPosition = ref::FindMethodByParamCount("via.Transform", "set_Position", 1);
    g_access.isHold = settingTd ? settingTd->find_field(kIsHoldField) : nullptr;
    g_access.settingType = api->typeof(kPositionSettingType);
    g_access.getComponent = ref::FindMethodByParamTypeName("via.GameObject", "getComponent", "Type");
    g_access.getFilterInfo = ref::FindMethodByParamCount("via.physics.CharacterController", "get_FilterInfo", 0);
    g_access.controllerType = api->typeof("via.physics.CharacterController");

    g_access.failed = !g_access.getCurrentScene || !g_access.findComponents || !g_access.getGameObject
        || !g_access.getTransform || !g_access.getPosition || !g_access.setPosition || !g_access.isHold
        || !g_access.settingType;

    ref::LogInfo("Player rig access: getCurrentScene=%p findComponents=%p getGameObject=%p getTransform=%p "
        "get_Position=%p set_Position=%p %s.%s=%p getComponent=%p CharacterController.get_FilterInfo=%p%s",
        (void*)g_access.getCurrentScene, (void*)g_access.findComponents, (void*)g_access.getGameObject,
        (void*)g_access.getTransform, (void*)g_access.getPosition, (void*)g_access.setPosition,
        kPositionSettingType, kIsHoldField, (void*)g_access.isHold, (void*)g_access.getComponent,
        (void*)g_access.getFilterInfo,
        g_access.failed ? " - AIM STATE AND RIG UNAVAILABLE, the lean stays on the camera while aiming" : "");
    if (!g_access.getComponent || !g_access.getFilterInfo || !g_access.controllerType) {
        ref::LogError("Player collision filter unavailable - the lean clamp cannot query and leans pass through unclamped");
    }
}

static void ReleaseCache() {
    if (g_filter) g_filter->release();
    if (g_root) g_root->release();
    if (g_setting) g_setting->release();
    g_filter = nullptr;
    g_root = nullptr;
    g_setting = nullptr;
}

static reframework::API::ManagedObject* ResolveFilter(void* go) {
    if (!g_access.getComponent || !g_access.getFilterInfo || !g_access.controllerType) return nullptr;
    auto controller = ref::CallMethodArg(g_access.getComponent, go, g_access.controllerType);
    if (!controller) return nullptr;
    return reinterpret_cast<reframework::API::ManagedObject*>(ref::CallMethod(g_access.getFilterInfo, controller));
}

static void ResolvePlayer() {
    if (g_access.failed) return;
    const uint64_t frame = ref::GetRenderFrame();
    if (frame < g_nextResolveFrame) return;
    g_nextResolveFrame = frame + kResolveIntervalFrames;

    auto sm = reframework::API::get()->get_native_singleton("via.SceneManager");
    if (!sm) return;
    auto scene = reinterpret_cast<reframework::API::ManagedObject*>(ref::CallMethod(g_access.getCurrentScene, sm));
    if (!scene) return;
    auto arr = reinterpret_cast<reframework::API::ManagedObject*>(
        ref::CallMethodArg(g_access.findComponents, scene, g_access.settingType));
    if (!arr) return;
    auto lenRet = arr->invoke("get_Length", ref::EmptyArgs());
    if (lenRet.exception_thrown || lenRet.dword == 0) {
        // Never drop the rig with an offset still on it.
        if (g_rig.written) RestoreRig();
        ReleaseCache();
        return;
    }
    auto setting = ref::ArrayGetValue(arr, 0);
    if (!setting || setting == g_setting) return;

    auto go = ref::CallMethod(g_access.getGameObject, setting);
    auto root = go ? reinterpret_cast<reframework::API::ManagedObject*>(ref::CallMethod(g_access.getTransform, go))
                   : nullptr;
    if (!root) return;

    auto filter = ResolveFilter(go);

    // Never swap the rig out from under an offset still on it.
    if (g_rig.written) RestoreRig();
    ReleaseCache();
    setting->add_ref();
    root->add_ref();
    if (filter) filter->add_ref();
    g_setting = setting;
    g_root = root;
    g_filter = filter;
    ref::LogInfo("Player rig: resolved (%u instance(s)), collision filter %s", lenRet.dword,
        filter ? "found" : "NOT FOUND - leans pass through unclamped");
}

static bool ReadIsHold(bool& out) {
    ResolvePlayer();
    if (!g_setting) return false;
    __try {
        out = g_access.isHold->get_data<bool>(g_setting);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsAiming() {
    bool hold = false;
    return ReadIsHold(hold) && hold;
}

bool RigAvailable() {
    ResolvePlayer();
    return g_root != nullptr;
}

reframework::API::ManagedObject* PlayerCollisionFilter() {
    ResolvePlayer();
    return g_filter;
}

static bool ReadRootPosition(float out[3]) {
    reframework::InvokeRet ret;
    if (!ref::TryInvoke(g_access.getPosition, g_root, ret)) return false;
    const float* p = reinterpret_cast<const float*>(&ret.bytes[0]);
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
    return true;
}

static bool WriteRootPosition(const float p[3]) {
    // via.vec3 is 16-byte aligned in RE Engine, so pass 4 floats.
    alignas(16) float v[4] = {p[0], p[1], p[2], 0.f};
    auto ret = ref::InvokeMethodWithArg(g_access.setPosition, g_root, (void*)&v[0]);
    return !ret.exception_thrown;
}

bool WriteRig(const float worldOffset[3]) {
    if (!g_root) return false;
    float p[3];
    if (!ReadRootPosition(p)) return false;
    const float moved[3] = {p[0] + worldOffset[0], p[1] + worldOffset[1], p[2] + worldOffset[2]};
    if (!WriteRootPosition(moved)) return false;
    for (int i = 0; i < 3; i++) g_rig.offset[i] = worldOffset[i];
    g_rig.written = true;
    return true;
}

// The offset comes back off rather than the old position going back on, so
// anything the game moved the player by in between is kept.
void RestoreRig() {
    if (!g_rig.written) return;
    g_rig.written = false;
    if (!g_root) return;
    float p[3];
    if (!ReadRootPosition(p)) {
        ref::LogError("Player rig: could not read the root to take the lean back off");
        return;
    }
    const float back[3] = {p[0] - g_rig.offset[0], p[1] - g_rig.offset[1], p[2] - g_rig.offset[2]};
    if (!WriteRootPosition(back)) ref::LogError("Player rig: could not write the root to take the lean back off");
}

bool UnzoomedFovDegrees(float& out) {
    bool hold = false;
    if (ReadIsHold(hold)) {
        const uint64_t now = cameraunlock::time::QpcNowMicros();
        if (hold) {
            g_fov.hipSinceUs = 0;
        } else {
            if (g_fov.hipSinceUs == 0) g_fov.hipSinceUs = now;
            if (!g_fov.has || now - g_fov.hipSinceUs >= kHipSettleUs) {
                const float live = ref::GetCameraResolver().ResolveFovDegrees(ref::GetCachedCamera());
                if (live > 1.f && live < 179.f) {
                    g_fov.degrees = live;
                    g_fov.has = true;
                }
            }
        }
    }
    if (!g_fov.has) return false;
    out = g_fov.degrees;
    return true;
}

} // namespace RE9HT
