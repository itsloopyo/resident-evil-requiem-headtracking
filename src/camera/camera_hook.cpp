#include "pch.h"
#include "camera_hook.h"
#include "camera_internal.h"
#include "gui_compensation.h"
#include "gui_diagnostics.h"
#include "game_state_detector.h"
#include "core/mod.h"
#include "core/logger.h"

#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/reframework/re_math.h>
#include <cameraunlock/math/smoothing_utils.h>
#include <reframework/API.hpp>

#include <string>

namespace RE9HT {

namespace ref = cameraunlock::reframework;

constexpr int TX_WORLDMATRIX_OFFSET = 0x80;

// --- Shared per-frame state (extern-declared in camera_internal.h) ---

CrosshairProjection g_crosshair;
CleanCameraMatrix g_cleanCameraMatrix;
float g_C[3][3] = {};
bool g_C_valid = false;
float g_posCleanX = 0.f;
float g_posCleanY = 0.f;
float g_posCleanZ = 0.f;

// Per-frame flag: set true when OnPreBeginRendering applies head tracking.
static bool g_trackingAppliedThisFrame = false;

// Primary camera pointer — detour skips this camera (3D renderer).
static void* g_primaryCamera = nullptr;

// Saved game rotation — what the game INTENDED before we modified it
static struct {
    Matrix4x4f gameMatrix;
    bool hasGameMatrix = false;
} g_saved;

// Method cache for the camera chain
static struct {
    reframework::API::Method* getMainView = nullptr;
    reframework::API::Method* getPrimaryCamera = nullptr;
    reframework::API::Method* getGameObject = nullptr;
    reframework::API::Method* getTransform = nullptr;
    reframework::API::Method* getCameraFov = nullptr;
    bool initialized = false;
    bool failed = false;
    bool hookWorking = false;
} g_fn;

// Per-frame transform cache
static void* g_cachedTransform = nullptr;

static void* ResolveCameraTransform() {
    const auto& api = reframework::API::get();
    auto sm = api->get_native_singleton("via.SceneManager");
    if (!sm) return nullptr;
    auto mv = ref::CallMethod(g_fn.getMainView, sm);
    if (!mv) return nullptr;
    auto cam = ref::CallMethod(g_fn.getPrimaryCamera, mv);
    if (!cam) return nullptr;
    auto go = ref::CallMethod(g_fn.getGameObject, cam);
    if (!go) return nullptr;
    return ref::CallMethod(g_fn.getTransform, go);
}

// Exposed for gui_diagnostics via camera_internal.h
void* ResolveCameraTransformInternal() {
    return ResolveCameraTransform();
}

static void* GetCameraTransformCached() {
    if (g_cachedTransform) return g_cachedTransform;
    g_cachedTransform = ResolveCameraTransform();
    return g_cachedTransform;
}

// --- Core head tracking application ---

static void ApplyHeadTracking(Matrix4x4f* worldMat) {
    float yaw, pitch, roll;
    if (!Mod::Instance().GetProcessedRotation(yaw, pitch, roll)) return;

    // Save pre-rotation axes for position offset
    Matrix4x4f preRotationAxes = *worldMat;

    // --- Rotation ---
    float yr = -yaw * DEG_TO_RAD;
    float pr = pitch * DEG_TO_RAD;
    float rr = roll * DEG_TO_RAD;

    if (Mod::Instance().IsWorldSpaceYaw()) {
        // Horizon-locked yaw: M'' = R_pitchroll * M * R_yaw
        float cy = cosf(yr), sy = -sinf(yr);
        for (int r = 0; r < 3; r++) {
            float x = worldMat->m[r][0];
            float z = worldMat->m[r][2];
            worldMat->m[r][0] = x * cy - z * sy;
            worldMat->m[r][2] = x * sy + z * cy;
        }

        float hp = pr * 0.5f, hr = rr * 0.5f;
        REQuat qx = {sinf(hp), 0, 0, cosf(hp)};
        REQuat qz = {0, 0, sinf(hr), cosf(hr)};
        REQuat qPR = QuatNorm(QuatMul(qx, qz));
        float prRot[3][3];
        QuatToMatrix3x3(qPR, prRot);

        for (int c = 0; c < 3; c++) {
            float c0 = worldMat->m[0][c];
            float c1 = worldMat->m[1][c];
            float c2 = worldMat->m[2][c];
            worldMat->m[0][c] = prRot[0][0]*c0 + prRot[0][1]*c1 + prRot[0][2]*c2;
            worldMat->m[1][c] = prRot[1][0]*c0 + prRot[1][1]*c1 + prRot[1][2]*c2;
            worldMat->m[2][c] = prRot[2][0]*c0 + prRot[2][1]*c1 + prRot[2][2]*c2;
        }
    } else {
        // Camera-local: all axes relative to camera orientation
        float hy = yr * 0.5f, hp = pr * 0.5f, hr = rr * 0.5f;
        REQuat qy = {0, sinf(hy), 0, cosf(hy)};
        REQuat qx = {sinf(hp), 0, 0, cosf(hp)};
        REQuat qz = {0, 0, sinf(hr), cosf(hr)};
        REQuat q = QuatNorm(QuatMul(QuatMul(qy, qx), qz));

        float headRot[3][3];
        QuatToMatrix3x3(q, headRot);

        for (int c = 0; c < 3; c++) {
            float c0 = worldMat->m[0][c];
            float c1 = worldMat->m[1][c];
            float c2 = worldMat->m[2][c];
            worldMat->m[0][c] = headRot[0][0]*c0 + headRot[0][1]*c1 + headRot[0][2]*c2;
            worldMat->m[1][c] = headRot[1][0]*c0 + headRot[1][1]*c1 + headRot[1][2]*c2;
            worldMat->m[2][c] = headRot[2][0]*c0 + headRot[2][1]*c1 + headRot[2][2]*c2;
        }
    }

    // --- Position (6DOF) ---
    float px, py, pz;
    if (Mod::Instance().GetPositionOffset(px, py, pz)) {
        px = -px;
        const Matrix4x4f& gm = preRotationAxes;
        worldMat->m[3][0] += px * gm.m[0][0] + py * gm.m[1][0] + pz * gm.m[2][0];
        worldMat->m[3][1] += px * gm.m[0][1] + py * gm.m[1][1] + pz * gm.m[2][1];
        worldMat->m[3][2] += px * gm.m[0][2] + py * gm.m[1][2] + pz * gm.m[2][2];
    }
}

// --- Camera controller hooks (save/restore) ---

static int CameraUpdatePreHook(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    g_cachedTransform = nullptr;

    if (!g_saved.hasGameMatrix || !Mod::Instance().IsEnabled()) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    void* transform = nullptr;
    __try { transform = GetCameraTransformCached(); } __except(EXCEPTION_EXECUTE_HANDLER) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }
    if (!transform) return REFRAMEWORK_HOOK_CALL_ORIGINAL;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + TX_WORLDMATRIX_OFFSET);
    __try {
        *worldMat = g_saved.gameMatrix;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

static void CameraUpdatePostHook(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    g_fn.hookWorking = true;

    void* transform = nullptr;
    __try { transform = GetCameraTransformCached(); } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + TX_WORLDMATRIX_OFFSET);
    __try {
        g_saved.gameMatrix = *worldMat;
        g_saved.hasGameMatrix = true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    static bool s_loggedOnce = false;
    if (!s_loggedOnce) {
        REQuat q = MatrixToQuat(g_saved.gameMatrix);
        Logger::Instance().Info("Hook save/restore active: gameQ=%.3f %.3f %.3f %.3f", q.x, q.y, q.z, q.w);
        s_loggedOnce = true;
    }
}

// --- Camera controller discovery ---

static void TryHookCameraController() {
    const auto& api = reframework::API::get();
    auto tdb = api->tdb();

    static const char* controllerTypes[] = {
        "requiem.PlayerCameraController",
        "requiem.camera.PlayerCameraController",
        "app.PlayerCameraController",
        "app.camera.PlayerCameraController",
    };

    static const char* methodNames[] = {
        "onCameraUpdate",
        "lateUpdate",
        "update",
    };

    for (auto typeName : controllerTypes) {
        auto type = tdb->find_type(typeName);
        if (!type) continue;
        for (auto methodName : methodNames) {
            auto method = type->find_method(methodName);
            if (!method) continue;
            auto id = method->add_hook(CameraUpdatePreHook, CameraUpdatePostHook, false);
            Logger::Instance().Info("Hooked %s.%s (id=%u)", typeName, methodName, id);
            return;
        }
    }

    // Walk parent chain to discover camera controller dynamically
    Logger::Instance().Info("Camera controller: hardcoded names failed, walking parent chain...");

    void* camTransform = ResolveCameraTransform();
    if (camTransform) {
        auto txMo = reinterpret_cast<reframework::API::ManagedObject*>(camTransform);
        for (int depth = 0; depth < 8; depth++) {
            auto goRet = txMo->invoke("get_GameObject", ref::EmptyArgs());
            if (goRet.exception_thrown || !goRet.ptr) break;
            auto goMo = reinterpret_cast<reframework::API::ManagedObject*>(goRet.ptr);

            char goName[128] = "?";
            auto nameRet = goMo->invoke("get_Name", ref::EmptyArgs());
            if (!nameRet.exception_thrown && nameRet.ptr) {
                ref::ReadManagedString(nameRet.ptr, goName, sizeof(goName));
            }

            auto compsRet = goMo->invoke("get_Components", ref::EmptyArgs());
            if (compsRet.exception_thrown || !compsRet.ptr) {
                Logger::Instance().Info("  parent[%d] GO=\"%s\": no components", depth, goName);
            } else {
                auto compArr = reinterpret_cast<reframework::API::ManagedObject*>(compsRet.ptr);
                auto lenRet = compArr->invoke("get_Length", ref::EmptyArgs());
                uint32_t compCount = lenRet.exception_thrown ? 0 : lenRet.dword;
                Logger::Instance().Info("  parent[%d] GO=\"%s\": %u components", depth, goName, compCount);

                for (uint32_t i = 0; i < compCount && i < 32; i++) {
                    auto comp = ref::ArrayGetValue(compArr, (int)i);
                    if (!comp) continue;
                    auto compTd = comp->get_type_definition();
                    if (!compTd) continue;
                    const char* cns = compTd->get_namespace();
                    const char* cnm = compTd->get_name();
                    if (!cns) cns = "";
                    if (!cnm) cnm = "?";
                    Logger::Instance().Info("    [%u] %s.%s", i, cns, cnm);

                    if (cnm && strstr(cnm, "Camera") && strstr(cnm, "Controller")) {
                        char fullName[256];
                        snprintf(fullName, sizeof(fullName), "%s.%s", cns, cnm);
                        Logger::Instance().Info("  -> Candidate camera controller: %s", fullName);

                        auto candidateType = tdb->find_type(fullName);
                        if (candidateType) {
                            for (auto mn : methodNames) {
                                auto m = candidateType->find_method(mn);
                                if (!m) continue;
                                auto id = m->add_hook(CameraUpdatePreHook, CameraUpdatePostHook, false);
                                Logger::Instance().Info("  -> Hooked %s.%s (id=%u)", fullName, mn, id);
                                return;
                            }
                        }
                    }
                }
            }

            auto parentRet = txMo->invoke("get_Parent", ref::EmptyArgs());
            if (parentRet.exception_thrown || !parentRet.ptr) break;
            txMo = reinterpret_cast<reframework::API::ManagedObject*>(parentRet.ptr);
        }
    }

    Logger::Instance().Warning("Camera controller hook not found — aim decoupling relies on PostBeginRendering restore");
}

// --- Initialization ---

static bool InitCachedFunctions() {
    if (g_fn.initialized) return !g_fn.failed;
    g_fn.initialized = true;

    const auto& api = reframework::API::get();
    auto tdb = api->tdb();
    auto smType = tdb->find_type("via.SceneManager");
    auto svType = tdb->find_type("via.SceneView");
    auto camType = tdb->find_type("via.Camera");
    auto goType = tdb->find_type("via.GameObject");

    if (!smType || !svType || !camType || !goType) { g_fn.failed = true; return false; }

    g_fn.getMainView = smType->find_method("get_MainView");
    g_fn.getPrimaryCamera = svType->find_method("get_PrimaryCamera");
    g_fn.getGameObject = camType->find_method("get_GameObject");
    g_fn.getTransform = goType->find_method("get_Transform");
    g_fn.getCameraFov = camType->find_method("get_FOV");

    if (!g_fn.getMainView || !g_fn.getPrimaryCamera || !g_fn.getGameObject || !g_fn.getTransform) {
        g_fn.failed = true;
        return false;
    }
    if (!g_fn.getCameraFov) {
        Logger::Instance().Error("via.Camera.get_FOV method not found — GUI marker compensation will be disabled");
    }

    TryHookCameraController();
    DiscoverGUICameraAccess();
    InitGUICompensationMethods();

    Logger::Instance().Info("Methods cached");
    return true;
}

// --- Public API ---

const CrosshairProjection& GetCrosshairProjection() { return g_crosshair; }

void OnPreBeginRendering() {
    if (!InitCachedFunctions()) return;
    if (!Mod::Instance().IsEnabled()) return;
    if (!IsInGameplay()) return;
    if (ShouldRecenter()) {
        Mod::Instance().Recenter();
    }

    void* transform = nullptr;
    __try { transform = GetCameraTransformCached(); } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + TX_WORLDMATRIX_OFFSET);

    // Save the clean matrix
    g_cleanCameraMatrix.matrix = *worldMat;
    g_cleanCameraMatrix.valid = true;

    ApplyHeadTracking(worldMat);
    g_trackingAppliedThisFrame = true;

    // Compute rotation differential C = R_head * R_clean^T
    ComputeCleanToHeadRotation(g_cleanCameraMatrix.matrix, *worldMat, g_C);
    g_C_valid = true;

    // Position delta in clean-camera-local space
    {
        const Matrix4x4f& clean = g_cleanCameraMatrix.matrix;
        float dwx = worldMat->m[3][0] - clean.m[3][0];
        float dwy = worldMat->m[3][1] - clean.m[3][1];
        float dwz = worldMat->m[3][2] - clean.m[3][2];
        g_posCleanX = dwx*clean.m[0][0] + dwy*clean.m[0][1] + dwz*clean.m[0][2];
        g_posCleanY = dwx*clean.m[1][0] + dwy*clean.m[1][1] + dwz*clean.m[1][2];
        g_posCleanZ = dwx*clean.m[2][0] + dwy*clean.m[2][1] + dwz*clean.m[2][2];
    }

    // Crosshair projection: where the aim point appears on the head-tracked screen
    {
        const Matrix4x4f& clean = g_cleanCameraMatrix.matrix;
        const Matrix4x4f& head = *worldMat;

        constexpr float kAimDist = 50.0f;
        float aimPtX = clean.m[3][0] + kAimDist * clean.m[2][0];
        float aimPtY = clean.m[3][1] + kAimDist * clean.m[2][1];
        float aimPtZ = clean.m[3][2] + kAimDist * clean.m[2][2];

        float dx = aimPtX - head.m[3][0];
        float dy = aimPtY - head.m[3][1];
        float dz = aimPtZ - head.m[3][2];

        float vx = dx * head.m[0][0] + dy * head.m[0][1] + dz * head.m[0][2];
        float vy = dx * head.m[1][0] + dy * head.m[1][1] + dz * head.m[1][2];
        float vz = dx * head.m[2][0] + dy * head.m[2][1] + dz * head.m[2][2];

        if (vz > 1e-4f) {
            float rawTanRight = vx / vz;
            float rawTanUp = vy / vz;

            // Read FOV from live camera
            float rawFov = g_crosshair.fovDegrees;
            auto sm = reframework::API::get()->get_native_singleton("via.SceneManager");
            if (sm && g_fn.getMainView && g_fn.getPrimaryCamera && g_fn.getCameraFov) {
                auto mv = g_fn.getMainView->invoke(
                    reinterpret_cast<reframework::API::ManagedObject*>(sm), ref::EmptyArgs());
                if (!mv.exception_thrown && mv.ptr) {
                    auto cam = g_fn.getPrimaryCamera->invoke(
                        reinterpret_cast<reframework::API::ManagedObject*>(mv.ptr), ref::EmptyArgs());
                    if (!cam.exception_thrown && cam.ptr) {
                        auto fov = g_fn.getCameraFov->invoke(
                            reinterpret_cast<reframework::API::ManagedObject*>(cam.ptr), ref::EmptyArgs());
                        if (!fov.exception_thrown) {
                            float fovDeg = 0.f;
                            if (fov.f >= 10.f && fov.f <= 170.f) fovDeg = fov.f;
                            else { float fromD = static_cast<float>(fov.d); if (fromD >= 10.f && fromD <= 170.f) fovDeg = fromD; }
                            if (fovDeg > 10.f) rawFov = fovDeg;
                        }
                    }
                }
            }

            // Smooth screen-space projection values to eliminate jitter from
            // perspective division noise and per-frame FOV fluctuations.
            // Uses the same baseline smoothing factor as the rotation pipeline.
            float dt = Mod::Instance().GetLastDeltaTime();
            constexpr float kCrosshairSmoothing = static_cast<float>(cameraunlock::math::kBaselineSmoothing);
            float t = cameraunlock::math::CalculateSmoothingFactor(kCrosshairSmoothing, dt);

            static float s_smoothedTanRight = 0.f;
            static float s_smoothedTanUp = 0.f;
            static float s_smoothedFov = 75.f;
            static bool s_initialized = false;

            if (!s_initialized) {
                s_smoothedTanRight = rawTanRight;
                s_smoothedTanUp = rawTanUp;
                s_smoothedFov = rawFov;
                s_initialized = true;
            } else {
                s_smoothedTanRight = cameraunlock::math::Lerp(s_smoothedTanRight, rawTanRight, t);
                s_smoothedTanUp = cameraunlock::math::Lerp(s_smoothedTanUp, rawTanUp, t);
                s_smoothedFov = cameraunlock::math::Lerp(s_smoothedFov, rawFov, t);
            }

            g_crosshair.tanRight = s_smoothedTanRight;
            g_crosshair.tanUp = s_smoothedTanUp;
            g_crosshair.fovDegrees = s_smoothedFov;
            g_crosshair.valid = g_crosshair.fovDegrees > 10.f;

            float roll = 0.f, yaw = 0.f, pitch = 0.f;
            Mod::Instance().GetProcessedRotation(yaw, pitch, roll);
            g_crosshair.rollDegrees = roll;
        } else {
            g_crosshair.valid = false;
        }

        static int s_projFrame = 0;
        if ((s_projFrame++ % 120) == 0) {
            Logger::Instance().Info("Crosshair proj: tanR=%.4f tanU=%.4f vz=%.2f fov=%.1f valid=%d | "
                "clean fwd=(%.3f,%.3f,%.3f) pos=(%.1f,%.1f,%.1f) | "
                "head fwd=(%.3f,%.3f,%.3f) pos=(%.1f,%.1f,%.1f)",
                g_crosshair.tanRight, g_crosshair.tanUp, vz, g_crosshair.fovDegrees, g_crosshair.valid,
                clean.m[2][0], clean.m[2][1], clean.m[2][2],
                clean.m[3][0], clean.m[3][1], clean.m[3][2],
                head.m[2][0], head.m[2][1], head.m[2][2],
                head.m[3][0], head.m[3][1], head.m[3][2]);
        }
    }
}

void OnPostBeginRendering() {
    if (!g_trackingAppliedThisFrame) return;
    g_trackingAppliedThisFrame = false;

    if (!g_cleanCameraMatrix.valid) return;

    void* transform = nullptr;
    __try { transform = ResolveCameraTransform(); } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + TX_WORLDMATRIX_OFFSET);
    __try {
        // Restore clean ROTATION but keep head-tracked POSITION.
        Matrix4x4f restored = g_cleanCameraMatrix.matrix;
        restored.m[3][0] = worldMat->m[3][0];
        restored.m[3][1] = worldMat->m[3][1];
        restored.m[3][2] = worldMat->m[3][2];
        *worldMat = restored;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    g_cachedTransform = nullptr;
}

} // namespace RE9HT
