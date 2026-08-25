#include "pch.h"
#include "camera_hook.h"
#include "aim_trace.h"
#include "camera_internal.h"
#include "flashlight_hook.h"
#include "gui_compensation.h"
#include "game_state_detector.h"
#include "core/mod.h"
#include "core/logger.h"

#include <cameraunlock/math/smoothing_utils.h>
#include <cameraunlock/reframework/camera_chain.h>
#include <cameraunlock/reframework/camera_controller_hook.h>
#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/reframework/re_math.h>
#include <reframework/API.hpp>

namespace RE9HT {

namespace ref = cameraunlock::reframework;

constexpr float kDegToRadLocal = 0.0174532925f;

// --- Shared per-frame state (extern-declared in camera_internal.h) ---

CrosshairProjection g_crosshair;
MarkerProjection g_marker;
CleanCameraMatrix g_cleanCameraMatrix;
float g_C[3][3] = {};
bool g_C_valid = false;
float g_posCleanX = 0.f;
float g_posCleanY = 0.f;
float g_posCleanZ = 0.f;
float g_headPos[3] = {};

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

// Where the aim points, as view tangents in the drawn frame.
//
// The reticle marks the clean aim DIRECTION projected through the head-rotated
// view. It deliberately carries no lean parallax.
//
// Marking the aim POINT is more correct on paper: the shot lands at
// clean.pos + distance * clean.forward and stays there however the head leans,
// so from an eye that has moved sideways the impact is no longer straight
// ahead, and a reticle that ignores that sits off the hole by lean/distance.
// That version was built, and its every ingredient was verified except one -
// the sign of the vertical lean term. Head pitch and head rise share a single
// formula, so they cannot need opposite signs, yet in game one was right only
// when the other was wrong, and the contradiction was never resolved. The
// distance came from a raycast that guessed which surface a bullet stops on,
// which took three attempts to stop measuring level-streaming volumes.
//
// So the parallax is out. What it leaves uncorrected is lean/distance, which is
// bounded and shrinks with range: at the leans this game's limits allow, a few
// degrees at conversational distance and under one at the far end of a room. It
// is never large, never depends on a sign nobody could pin down, and it cannot
// fly off. Every remaining term - the rotation, the canvas mapping, the
// projection scale - is confirmed against the engine's own matrices.
//
// Bringing parallax back needs the vertical sign settled by measurement rather
// than by flipping it in front of a player, and a distance that is known to be
// the surface the shot stops on rather than inferred from a collision layer.
// The lean parallax: the difference between projecting the impact point and
// projecting the aim direction, through the head-tracked view.
//
// Split from the rotation term rather than folded into one projection so the two
// can carry different signs, which they must. The rotation sign is confirmed in
// game. The parallax sign that falls out of the geometry is the opposite of what
// the game does, so the negation below is fitted to where the hole lands rather
// than derived - a derivation needs to know which row of the camera basis points
// left and which way the canvas runs vertically, and neither follows from the
// camera matrix.
//
// The 1/distance shape is measured, not assumed. Run with a fixed two metres in
// place of the trace, the reticle drifted with the lean beyond that range and
// against it inside, crossing zero exactly at the hardcoded value - which is
// what a correction that should scale as lean/distance looks like when it is
// held constant.
static bool ProjectLeanParallax(const Matrix4x4f& clean, const Matrix4x4f& head,
                                float aimDist, float& parallaxR, float& parallaxU) {
    float rotR = 0.f, rotU = 0.f;
    if (!ref::ProjectForwardToViewTangents(clean, head, rotR, rotU)) return false;

    const float dx = clean.m[3][0] + clean.m[2][0] * aimDist - head.m[3][0];
    const float dy = clean.m[3][1] + clean.m[2][1] * aimDist - head.m[3][1];
    const float dz = clean.m[3][2] + clean.m[2][2] * aimDist - head.m[3][2];
    const float vx = dx * head.m[0][0] + dy * head.m[0][1] + dz * head.m[0][2];
    const float vy = dx * head.m[1][0] + dy * head.m[1][1] + dz * head.m[1][2];
    const float vz = dx * head.m[2][0] + dy * head.m[2][1] + dz * head.m[2][2];
    if (!(vz > 0.1f)) return false;

    parallaxR = -(vx / vz - rotR);
    parallaxU = -(vy / vz - rotU);
    return true;
}

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
        // x is negated by the shared helper, which takes offsetX and applies
        // -offsetX. Passing -px here cancelled that, so this mod was the only
        // one in the fleet whose lateral lean was not mirrored at the engine
        // boundary - the comment that used to sit here claimed the helper did
        // no negation and recorded the resulting sign as confirmed in game.
        // A mirrored lean moves the camera opposite to the head, which reads as
        // working until something anchored in the world has to agree with it:
        // the reticle then tracks the way the head leans instead of against it.
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

static reframework::API::Method* g_getProjectionMatrix = nullptr;

static bool ReadCameraMat4(reframework::API::Method* method, void* camera, Matrix4x4f& out) {
    if (!method || !camera) return false;
    auto ret = method->invoke(
        reinterpret_cast<reframework::API::ManagedObject*>(camera), ref::EmptyArgs());
    if (ret.exception_thrown) return false;
    memcpy(&out, &ret.bytes[0], sizeof(Matrix4x4f));
    return true;
}

static bool InitCachedFunctions() {
    static bool s_attempted = false;
    if (s_attempted) return !g_cameraResolver.HasFailed();
    s_attempted = true;

    if (!g_cameraResolver.Initialize()) return false;

    g_getProjectionMatrix = ref::FindMethodByParamCount("via.Camera", "get_ProjectionMatrix", 0);
    if (!g_getProjectionMatrix) {
        Logger::Instance().Error(
            "via.Camera.get_ProjectionMatrix not found - the reticle has no scale to place "
            "itself with and stays at the canvas centre");
    }

    InitAimTrace();
    InitGUICompensationMethods();
    InitFlashlightAccess();

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

    // What we asked the camera to move by, for the readback in
    // OnPostBeginRendering. The parallax is computed from this; if the engine
    // only honours a fraction of it, the rendered eye moves less than we
    // compensate for and the correction comes out oversized by that ratio.
    g_headPos[0] = worldMat->m[3][0];
    g_headPos[1] = worldMat->m[3][1];
    g_headPos[2] = worldMat->m[3][2];

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

    // Where the shot lands, in the picture the head is looking at.
    //
    // Screen-space values are smoothed to eliminate jitter from perspective
    // division noise and per-frame FOV fluctuations, using the internal
    // projection-smoothing constant above.
    {
        const Matrix4x4f& clean = g_cleanCameraMatrix.matrix;
        const Matrix4x4f& head = *worldMat;

        void* camera = g_cameraResolver.ResolveCamera();
        Matrix4x4f proj{};
        const bool haveProj = ReadCameraMat4(g_getProjectionMatrix, camera, proj);

        float handR = 0.f, handU = 0.f;
        if (haveProj && ref::ProjectForwardToViewTangents(clean, head, handR, handU)) {
            float aimDist = 0.f;
            if (TryGetAimDistance(&clean.m[3][0], &clean.m[2][0], aimDist)) {
                float leanR = 0.f, leanU = 0.f;
                if (ProjectLeanParallax(clean, head, aimDist, leanR, leanU)) {
                    handR += leanR;
                    handU += leanU;
                }
            }
            float rawFov = g_cameraResolver.ResolveFovDegrees(camera);
            if (rawFov <= 0.f) rawFov = g_crosshair.fovDegrees;

            // Tangents scaled by the projection matrix's own [0][0] and [1][1].
            // The vertical negation is asymmetric with the horizontal one: this
            // is the sign that moves the reticle against head pitch, verified in
            // game, and flipping it to match sent the reticle off in the
            // direction of the pitch instead.
            const float rawNdcX = -handR * proj.m[0][0];
            const float rawNdcY = -handU * proj.m[1][1];

            static cameraunlock::math::SmoothedFloat s_ndcX;
            static cameraunlock::math::SmoothedFloat s_ndcY;
            static cameraunlock::math::SmoothedFloat s_fov;

            g_crosshair.ndcX = s_ndcX.Update(rawNdcX, kProjectionSmoothing, dt);
            g_crosshair.ndcY = s_ndcY.Update(rawNdcY, kProjectionSmoothing, dt);
            g_crosshair.fovDegrees = s_fov.Update(rawFov, kProjectionSmoothing, dt);
            g_crosshair.ndcPerTanX = proj.m[0][0];
            g_crosshair.ndcPerTanY = proj.m[1][1];
            g_crosshair.valid = true;

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
            Logger::Instance().Info("Crosshair proj: ndc=(%.4f,%.4f) fov=%.1f valid=%d | "
                "clean fwd=(%.3f,%.3f,%.3f) pos=(%.1f,%.1f,%.1f) | "
                "head fwd=(%.3f,%.3f,%.3f) pos=(%.1f,%.1f,%.1f)",
                g_crosshair.ndcX, g_crosshair.ndcY, g_crosshair.fovDegrees, g_crosshair.valid,
                clean.m[2][0], clean.m[2][1], clean.m[2][2],
                clean.m[3][0], clean.m[3][1], clean.m[3][2],
                head.m[2][0], head.m[2][1], head.m[2][2],
                head.m[3][0], head.m[3][1], head.m[3][2]);
        }
    }

    // Markers: rotation only, no lean. See MarkerProjection in the header.
    {
        float rawR = 0.f, rawU = 0.f;
        if (ref::ProjectForwardToViewTangents(g_cleanCameraMatrix.matrix, *worldMat, rawR, rawU)) {
            static cameraunlock::math::SmoothedFloat s_mR;
            static cameraunlock::math::SmoothedFloat s_mU;
            g_marker.tanRight = s_mR.Update(rawR, kProjectionSmoothing, dt);
            g_marker.tanUp = s_mU.Update(rawU, kProjectionSmoothing, dt);
            g_marker.valid = true;
        } else {
            g_marker.valid = false;
        }
    }

    ApplyFlashlightTracking();
}

void OnPostBeginRendering() {
    RestoreFlashlightTracking();

    if (!g_trackingAppliedThisFrame) return;
    g_trackingAppliedThisFrame = false;

    if (!g_cleanCameraMatrix.valid) return;

    void* transform = g_cameraResolver.ResolveTransform();
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + ref::kTransformWorldMatrixOffset);
    __try {
        // Restore the clean camera in full - POSITION as well as rotation.
        //
        // This is the only decoupling point the mod has: the player camera
        // controller hook has never matched a type on this game ("Camera
        // controller hook not found" in every log), so between here and the
        // next frame's render the transform is whatever this line leaves. It
        // used to leave the head-tracked position, and the game aims off that:
        // the shot converges on the leaned eye's axis while the round leaves
        // the un-leaned body, so reticle and impact agree at exactly one range
        // and splay apart either side of it, swapping sides as the player walks
        // through it. Head tracking must not move where bullets go.
        //
        // The lean still renders. Rotation is written and taken back at the
        // same two hooks and rotation is what the player sees, so the camera
        // matrix the renderer consumes is snapshotted between them; the
        // translation row is in that same matrix.
        *worldMat = g_cleanCameraMatrix.matrix;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    g_cachedTransform = nullptr;
}

} // namespace RE9HT
