#include "pch.h"
#include "crosshair.h"
#include "aim_trace.h"
#include "flashlight_hook.h"

#include <cameraunlock/math/smoothing_utils.h>
#include <cameraunlock/reframework/camera_pipeline.h>
#include <cameraunlock/reframework/gui_elements.h>
#include <cameraunlock/reframework/log_callback.h>
#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/reframework/plugin_mod.h>
#include <cameraunlock/reframework/re_math.h>

#include <reframework/API.hpp>
#include <cstring>

namespace RE9HT {

namespace ref = cameraunlock::reframework;

using ref::Matrix4x4f;

static CrosshairProjection g_crosshair;

const CrosshairProjection& GetCrosshairProjection() { return g_crosshair; }

static reframework::API::Method* g_getProjectionMatrix = nullptr;

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

static bool ReadCameraMat4(reframework::API::Method* method, void* camera, Matrix4x4f& out) {
    if (!method || !camera) return false;
    auto ret = method->invoke(
        reinterpret_cast<reframework::API::ManagedObject*>(camera), ref::EmptyArgs());
    if (ret.exception_thrown) return false;
    memcpy(&out, &ret.bytes[0], sizeof(Matrix4x4f));
    return true;
}

void InitCrosshairProjection() {
    g_getProjectionMatrix = ref::FindMethodByParamCount("via.Camera", "get_ProjectionMatrix", 0);
    if (!g_getProjectionMatrix) {
        ref::LogError(
            "via.Camera.get_ProjectionMatrix not found - the reticle has no scale to place "
            "itself with and stays at the canvas centre");
    }

    InitAimTrace();
    ref::InitGuiMethods();
    InitFlashlightAccess();
}

void OnFrameApplied(const Matrix4x4f& clean, const Matrix4x4f& head) {
    const float dt = ref::PluginMod::Instance().GetLastDeltaTime();

    // Where the shot lands, in the picture the head is looking at.
    //
    // Screen-space values are smoothed to eliminate jitter from perspective
    // division noise and per-frame FOV fluctuations, using the internal
    // projection-smoothing constant the rest of the pipeline shares.
    void* camera = ref::GetCachedCamera();
    if (!camera) camera = ref::GetCameraResolver().ResolveCamera();
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
        float rawFov = ref::GetCameraResolver().ResolveFovDegrees(camera);
        if (rawFov <= 0.f) rawFov = g_crosshair.fovDegrees;

        // Square pixels: the horizontal and vertical pixel focal lengths these
        // NDC factors become have to match. The RE3 build proved this
        // projection path can hand back [0][0] at half its true value, which
        // under-compensates yaw and drifts the reticle and the markers
        // horizontally, so [0][0] is not read at all - the horizontal factor
        // comes from the trusted [1][1] and the reference canvas aspect. Same
        // guard core applies as fx = fy in ComputeMarkerFocalLengths, which
        // covers every path but this one. Derived here, once, because both
        // consumers scale by it: the reticle NDC below and the marker path
        // through CanvasFocalLengths.
        const float ndcPerTanY = proj.m[1][1];
        const float ndcPerTanX =
            ndcPerTanY * (ref::kHalfReferenceCanvasHeight / ref::kHalfReferenceCanvasWidth);

        // The vertical negation is asymmetric with the horizontal one: this
        // is the sign that moves the reticle against head pitch, verified in
        // game, and flipping it to match sent the reticle off in the
        // direction of the pitch instead.
        const float rawNdcX = -handR * ndcPerTanX;
        const float rawNdcY = -handU * ndcPerTanY;

        static cameraunlock::math::SmoothedFloat s_ndcX;
        static cameraunlock::math::SmoothedFloat s_ndcY;
        static cameraunlock::math::SmoothedFloat s_fov;

        g_crosshair.ndcX = s_ndcX.Update(rawNdcX, ref::kProjectionSmoothing, dt);
        g_crosshair.ndcY = s_ndcY.Update(rawNdcY, ref::kProjectionSmoothing, dt);
        g_crosshair.fovDegrees = s_fov.Update(rawFov, ref::kProjectionSmoothing, dt);
        g_crosshair.ndcPerTanX = ndcPerTanX;
        g_crosshair.ndcPerTanY = ndcPerTanY;
        g_crosshair.valid = true;

        float roll = 0.f, yaw = 0.f, pitch = 0.f;
        ref::PluginMod::Instance().GetProcessedRotation(yaw, pitch, roll);
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
        ref::LogInfo("Crosshair proj: ndc=(%.4f,%.4f) fov=%.1f valid=%d | "
            "clean fwd=(%.3f,%.3f,%.3f) pos=(%.1f,%.1f,%.1f) | "
            "head fwd=(%.3f,%.3f,%.3f) pos=(%.1f,%.1f,%.1f)",
            g_crosshair.ndcX, g_crosshair.ndcY, g_crosshair.fovDegrees, g_crosshair.valid,
            clean.m[2][0], clean.m[2][1], clean.m[2][2],
            clean.m[3][0], clean.m[3][1], clean.m[3][2],
            head.m[2][0], head.m[2][1], head.m[2][2],
            head.m[3][0], head.m[3][1], head.m[3][2]);
    }

    ApplyFlashlightTracking();
}

void OnPostRestore() {
    RestoreFlashlightTracking();
}

} // namespace RE9HT
