#include "pch.h"
#include "camera_hook.h"
#include "camera_internal.h"
#include "gui_compensation.h"
#include "gui_diagnostics.h"
#include "game_state_detector.h"
#include "core/mod.h"
#include "core/logger.h"

#include <cameraunlock/math/smoothing_utils.h>
#include <cameraunlock/reframework/camera_chain.h>
#include <cameraunlock/reframework/camera_controller_hook.h>
#include <cameraunlock/reframework/re_math.h>
#include <reframework/API.hpp>

namespace RE9HT {

namespace ref = cameraunlock::reframework;

// --- Shared per-frame state (extern-declared in camera_internal.h) ---

CrosshairProjection g_crosshair;
MarkerProjection g_marker;
CleanCameraMatrix g_cleanCameraMatrix;
float g_C[3][3] = {};
bool g_C_valid = false;
float g_posCleanX = 0.f;
float g_posCleanY = 0.f;
float g_posCleanZ = 0.f;

// Per-frame flag: set true when OnPreBeginRendering applies head tracking.
static bool g_trackingAppliedThisFrame = false;

// Saved game rotation - what the game INTENDED before we modified it
static struct {
    Matrix4x4f gameMatrix;
    bool hasGameMatrix = false;
} g_saved;

static ref::CameraTransformResolver g_cameraResolver;

ref::CameraTransformResolver& CameraResolver() {
    return g_cameraResolver;
}

// Per-frame transform cache
static void* g_cachedTransform = nullptr;

static void* GetCameraTransformCached() {
    if (g_cachedTransform) return g_cachedTransform;
    g_cachedTransform = g_cameraResolver.ResolveTransform();
    return g_cachedTransform;
}

// --- Core head tracking application ---

static void ApplyHeadTracking(Matrix4x4f* worldMat) {
    float yaw, pitch, roll;
    if (!Mod::Instance().GetProcessedRotation(yaw, pitch, roll)) return;

    // Save pre-rotation axes for position offset
    Matrix4x4f preRotationAxes = *worldMat;

    float yr = -yaw * DEG_TO_RAD;
    float pr = pitch * DEG_TO_RAD;
    float rr = roll * DEG_TO_RAD;

    if (Mod::Instance().IsWorldSpaceYaw()) {
        ref::ApplyWorldSpaceHeadRotation(*worldMat, yr, pr, rr);
    } else {
        ref::ApplyCameraLocalHeadRotation(*worldMat, yr, pr, rr);
    }

    // --- Position (6DOF) ---
    float px, py, pz;
    if (Mod::Instance().GetPositionOffset(px, py, pz)) {
        ref::ApplyViewSpacePositionOffset(*worldMat, preRotationAxes, px, py, pz);
    }
}

// --- Camera controller hooks (save/restore) ---

static int CameraUpdatePreHook(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    g_cachedTransform = nullptr;

    if (!g_saved.hasGameMatrix || !Mod::Instance().IsEnabled()) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    void* transform = GetCameraTransformCached();
    if (!transform) return REFRAMEWORK_HOOK_CALL_ORIGINAL;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + ref::kTransformWorldMatrixOffset);
    __try {
        *worldMat = g_saved.gameMatrix;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

static void CameraUpdatePostHook(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    void* transform = GetCameraTransformCached();
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + ref::kTransformWorldMatrixOffset);
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

static const char* const kControllerCandidateTypes[] = {
    "requiem.PlayerCameraController",
    "requiem.camera.PlayerCameraController",
    "app.PlayerCameraController",
    "app.camera.PlayerCameraController",
};

static ref::CameraControllerHooker g_controllerHooker{
    kControllerCandidateTypes,
    static_cast<int>(sizeof(kControllerCandidateTypes) / sizeof(kControllerCandidateTypes[0])),
    CameraUpdatePreHook,
    CameraUpdatePostHook,
};

// The player camera controller component only exists once gameplay starts
// (the main menu camera carries only render/effect controllers), so
// discovery retries from gameplay frames instead of latching at init.
// Attempts are spaced out and capped to bound the per-attempt component
// logging the parent-chain walk produces.
static void TryHookCameraController(void* cameraTransform) {
    constexpr int kMaxAttempts = 5;
    constexpr int kRetryCooldownFrames = 120;

    if (g_controllerHooker.IsHooked()) return;
    if (g_controllerHooker.AttemptCount() >= kMaxAttempts) return;

    static int s_cooldown = 0;
    if (s_cooldown-- > 0) return;
    s_cooldown = kRetryCooldownFrames;

    if (!g_controllerHooker.TryHook(cameraTransform)
        && g_controllerHooker.AttemptCount() >= kMaxAttempts) {
        Logger::Instance().Warning(
            "Camera controller hook not found - aim decoupling relies on PostBeginRendering restore");
    }
}

// --- Initialization ---

static bool InitCachedFunctions() {
    static bool s_attempted = false;
    if (s_attempted) return !g_cameraResolver.HasFailed();
    s_attempted = true;

    if (!g_cameraResolver.Initialize()) return false;

    DiscoverGUICameraAccess();
    InitGUICompensationMethods();

    Logger::Instance().Info("Methods cached");
    return true;
}

// --- Public API ---

const CrosshairProjection& GetCrosshairProjection() { return g_crosshair; }
const MarkerProjection& GetMarkerProjection() { return g_marker; }

void OnPreBeginRendering() {
    // Before every gate below: the first-packet latch has to survive
    // AutoEnable=false, a menu, and a failed function cache, because those are
    // exactly the states a "no head tracking" report is trying to tell apart.
    Mod::Instance().LogFirstTrackerPose();

    if (!InitCachedFunctions()) return;
    if (!Mod::Instance().IsEnabled()) return;
    if (!IsInGameplay()) return;

    // Advance interpolation + smoothing once per render frame. Every
    // downstream consumer (ApplyHeadTracking, crosshair projection, GUI
    // marker compensation) reads cached values, so the rendered camera and
    // the smoother see the same wall-clock dt.
    Mod::Instance().TickFrame();

    void* transform = GetCameraTransformCached();
    if (!transform) return;

    TryHookCameraController(transform);

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + ref::kTransformWorldMatrixOffset);

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
        float delta[3] = {};
        ref::ComputeCleanLocalPositionDelta(g_cleanCameraMatrix.matrix, *worldMat, delta);
        g_posCleanX = delta[0];
        g_posCleanY = delta[1];
        g_posCleanZ = delta[2];
    }

    float dt = Mod::Instance().GetLastDeltaTime();
    // Internal projection-smoothing constant, deliberately independent of the user's tracking smoothing.
    constexpr float kProjectionSmoothing = 0.15f;

    // Crosshair projection: where the aim point appears on the head-tracked screen.
    // Screen-space values are smoothed to eliminate jitter from perspective
    // division noise and per-frame FOV fluctuations, using the internal
    // projection-smoothing constant above.
    {
        const Matrix4x4f& clean = g_cleanCameraMatrix.matrix;
        const Matrix4x4f& head = *worldMat;

        constexpr float kAimDist = 50.0f;
        float rawTanRight = 0.f, rawTanUp = 0.f;
        if (ref::ProjectAimToViewTangents(clean, head, kAimDist, rawTanRight, rawTanUp)) {
            // Read FOV from live camera; hold the previous value when the read fails.
            float rawFov = g_cameraResolver.ResolveFovDegrees();
            if (rawFov <= 0.f) rawFov = g_crosshair.fovDegrees;

            static cameraunlock::math::SmoothedFloat s_tanRight;
            static cameraunlock::math::SmoothedFloat s_tanUp;
            static cameraunlock::math::SmoothedFloat s_fov;

            g_crosshair.tanRight = s_tanRight.Update(rawTanRight, kProjectionSmoothing, dt);
            g_crosshair.tanUp = s_tanUp.Update(rawTanUp, kProjectionSmoothing, dt);
            g_crosshair.fovDegrees = s_fov.Update(rawFov, kProjectionSmoothing, dt);
            g_crosshair.valid = g_crosshair.fovDegrees > 10.f;

            float roll = 0.f, yaw = 0.f, pitch = 0.f;
            Mod::Instance().GetProcessedRotation(yaw, pitch, roll);
            g_crosshair.rollDegrees = roll;
        } else {
            g_crosshair.valid = false;
        }

        // Capped: the 120-frame interval alone streams for the whole
        // session, which buries the startup chain a user is asked to send.
        static int s_projFrame = 0;
        static int s_projFrameLeft = 5;
        if (s_projFrameLeft > 0 && (s_projFrame++ % 120) == 0) {
            s_projFrameLeft--;
            Logger::Instance().Info("Crosshair proj: tanR=%.4f tanU=%.4f fov=%.1f valid=%d | "
                "clean fwd=(%.3f,%.3f,%.3f) pos=(%.1f,%.1f,%.1f) | "
                "head fwd=(%.3f,%.3f,%.3f) pos=(%.1f,%.1f,%.1f)",
                g_crosshair.tanRight, g_crosshair.tanUp, g_crosshair.fovDegrees, g_crosshair.valid,
                clean.m[2][0], clean.m[2][1], clean.m[2][2],
                clean.m[3][0], clean.m[3][1], clean.m[3][2],
                head.m[2][0], head.m[2][1], head.m[2][2],
                head.m[3][0], head.m[3][1], head.m[3][2]);
        }
    }

    // Marker projection: rotation-only. OnPostBeginRendering restores clean
    // rotation but keeps the head-tracked position, so at GUI draw time the
    // game's projection matrix is (clean rotation, head position). Anything
    // the GUI projects through that matrix gets translation parallax for
    // free - leaning shifts the world anchor's screen position the same way
    // it shifts the rendered scene, so the marker tracks the target without
    // any help from us. Only rotation needs to be compensated manually
    // (because the rotation was reset to clean).
    {
        float rawTanRight = 0.f, rawTanUp = 0.f;
        if (ref::ProjectForwardToViewTangents(g_cleanCameraMatrix.matrix, *worldMat, rawTanRight, rawTanUp)) {
            static cameraunlock::math::SmoothedFloat s_tanRight;
            static cameraunlock::math::SmoothedFloat s_tanUp;

            g_marker.tanRight = s_tanRight.Update(rawTanRight, kProjectionSmoothing, dt);
            g_marker.tanUp = s_tanUp.Update(rawTanUp, kProjectionSmoothing, dt);
            g_marker.valid = true;
        } else {
            g_marker.valid = false;
        }
    }
}

void OnPostBeginRendering() {
    if (!g_trackingAppliedThisFrame) return;
    g_trackingAppliedThisFrame = false;

    if (!g_cleanCameraMatrix.valid) return;

    void* transform = g_cameraResolver.ResolveTransform();
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + ref::kTransformWorldMatrixOffset);
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
