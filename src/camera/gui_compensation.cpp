#include "pch.h"
#include "gui_compensation.h"
#include "crosshair.h"
#include "game_state_detector.h"

#include <cameraunlock/reframework/camera_pipeline.h>
#include <cameraunlock/reframework/gui_elements.h>
#include <cameraunlock/reframework/log_callback.h>
#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/reframework/plugin_mod.h>
#include <cameraunlock/reframework/re_math.h>

#include <reframework/API.hpp>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_set>

namespace RE9HT {

namespace ref = cameraunlock::reframework;

// Cap on unique GUI GameObject names recorded during identity logging.
constexpr size_t kMaxLoggedGuiIdentities = 100;

// "layout" - the node both the reticle write and the identity probe target.
constexpr uint32_t kLayoutChildIdx = 2;

// --- Canvas geometry ---

// Pixel focal lengths, from the camera's own projection matrix. Scaling the NDC
// factor against half the canvas in each axis carries the aspect ratio the
// engine actually renders with, so nothing here has to assume whether get_FOV
// means the vertical or the horizontal angle - see CrosshairProjection.
static void CanvasFocalLengths(float canvasW, float canvasH,
                               float& focalX, float& focalY) {
    const auto& crosshair = GetCrosshairProjection();
    focalX = crosshair.ndcPerTanX * canvasW * 0.5f;
    focalY = crosshair.ndcPerTanY * canvasH * 0.5f;
}

// --- GUI identity ---

// One-shot per GUI name. The reticle GUI has never been observed in a log, so
// which Gui_ui20* GUI it is was assumed rather than known, and the assumption
// was wrong: the offset was landing on Gui_ui2000 and Gui_ui2050, both HUD.
//
// A reticle marks the aim direction, and the aim direction projects to the
// canvas centre in the clean view, so the reticle GUI is the one whose layout
// node sits natively at the canvas centre. That is what this records, for every
// Gui_ui20* GUI, so the target can be identified from a session where the
// player aims rather than guessed at again.
static void LogGuiIdentity(reframework::API::ManagedObject* guiMo, const char* goName) {
    const auto& gui = ref::GetGuiMethods();
    if (!gui.ready || !gui.getPosition) return;

    static std::unordered_set<std::string> s_logged;
    if (s_logged.size() >= kMaxLoggedGuiIdentities) return;
    if (!s_logged.insert(std::string(goName)).second) return;

    float canvasW = 0.f, canvasH = 0.f;
    if (!ref::GetElementCanvasSize(guiMo, canvasW, canvasH)) return;

    uint32_t count = 0;
    auto arr = ref::FindPlayObjects(guiMo, count);
    if (!arr || count <= kLayoutChildIdx) return;

    auto layout = ref::ArrayGetValue(arr, (int)kLayoutChildIdx);
    if (!layout) return;

    char childName[64] = "?";
    auto nameRet = layout->invoke("get_Name", ref::EmptyArgs());
    if (!nameRet.exception_thrown && nameRet.ptr) {
        ref::ReadManagedString(nameRet.ptr, childName, sizeof(childName));
    }

    float px = 0.f, py = 0.f;
    if (!ref::GetTransformPosition(layout, px, py)) return;

    bool atCentre = fabsf(px - canvasW * 0.5f) < 1.f && fabsf(py - canvasH * 0.5f) < 1.f;
    ref::LogInfo(
        "GUI identity: \"%s\" canvas=(%.0fx%.0f) objects=%u child[%u]=\"%s\" nativePos=(%.1f,%.1f)%s",
        goName, canvasW, canvasH, count, kLayoutChildIdx, childName, px, py,
        atCentre ? " [at canvas centre - reticle shape]" : " [not at centre - screen-anchored HUD]");
}

// Canvas offsets from the aim tangents.
//
// The camera basis rows are (left, up, forward), confirmed on logged clean/head
// forward pairs - here the head is pitched DOWN, so the aim sits ABOVE the view
// centre, and tanUp comes back positive:
//   clean fwd=(0.907,0.074,-0.415) head fwd=(0.921,-0.117,-0.373) tanU=+0.2079
// and here the head turned left while the aim projects right, so row 0 is left:
//   clean fwd=(0.423,0,-0.906) head fwd=(0.298,0.089,-0.950) tanR=-0.1359
//
// Hence the horizontal negates and the vertical does not. The vertical sign is
// empirical, not derived: it depends on which way the GUI canvas runs, and the
// canvas both places markers and rotates them, so reasoning it out from the
// design-time layout offsets got it backwards once already. Flipping it made
// the reticle track the head instead of the aim. Leave it as measured.
static inline float AimCanvasOffsetX(float tanRight, float focalX) { return -tanRight * focalX; }
static inline float AimCanvasOffsetY(float tanUp, float focalY)    { return  tanUp * focalY; }

// --- Crosshair compensation ---

// Moves the reticle to where the clean aim direction projects in the
// head-rotated view, so shots land where the reticle is drawn.
//
// The absolute write is canvas centre plus the aim offset, with no baseline
// capture, because a reticle marks the aim direction and that direction
// projects to the canvas centre in the clean view. A GUI whose layout node is
// not natively at the centre is not a reticle, which is what LogGuiIdentity
// records.
static void ApplyCrosshairOffset(reframework::API::ManagedObject* guiMo, const char* goName) {
    const auto& gui = ref::GetGuiMethods();
    if (!guiMo || !gui.ready) return;

    const auto& crosshair = GetCrosshairProjection();
    if (!crosshair.valid || !ref::PluginMod::Instance().IsEnabled() || !IsInGameplay()) return;

    float canvasW = 0.f, canvasH = 0.f;
    if (!ref::GetElementCanvasSize(guiMo, canvasW, canvasH)) return;
    const float centreX = canvasW * 0.5f;
    const float centreY = canvasH * 0.5f;
    // NDC runs -1..+1 with y up; the canvas runs 0..height with y down.
    const float deltaX =  crosshair.ndcX * canvasW * 0.5f;
    const float deltaY = -crosshair.ndcY * canvasH * 0.5f;

    uint32_t count = 0;
    auto arr = ref::FindPlayObjects(guiMo, count);
    if (!arr || count <= kLayoutChildIdx) return;

    auto layoutElem = ref::ArrayGetValue(arr, (int)kLayoutChildIdx);
    if (!layoutElem) return;

    // Where the reticle sat before this write. The absolute write below assumes
    // the game parks its reticle at the canvas centre and leaves it there, so
    // that centre plus our offset is the whole story. If the game moves it
    // itself - for weapon sway, recoil, or a sight whose convergence depends on
    // range - then overwriting it discards that and leaves an error the mod
    // cannot see. One read says which.
    float beforeX = 0.f, beforeY = 0.f;
    ref::GetTransformPosition(layoutElem, beforeX, beforeY);

    ref::SetTransformPosition(layoutElem, centreX + deltaX, centreY + deltaY);

    // Capped: the 120-frame interval alone streams for the whole
    // session, which buries the startup chain a user is asked to send.
    static int s_diagFrame = 0;
    static int s_diagLeft = 5;
    if (s_diagLeft > 0 && (s_diagFrame++ % 120) == 0) {
        s_diagLeft--;
        ref::LogInfo("CROSSHAIR \"%s\": canvas=(%.0fx%.0f) centre=(%.1f,%.1f) "
            "ndc=(%.4f,%.4f) delta=(%.1f,%.1f) before=(%.1f,%.1f) wrote=(%.1f,%.1f)",
            goName, canvasW, canvasH, centreX, centreY,
            crosshair.ndcX, crosshair.ndcY, deltaX, deltaY,
            beforeX, beforeY, centreX + deltaX, centreY + deltaY);
    }
}

// --- Marker compensation ---

// The GUI writes each marker's projected screen position into that marker's own
// node ("type0", flat PlayObject index in the 800s), and every ancestor up to
// "main" (child[1]) sits at (0,0). So a write to "main" transforms the whole set
// of markers at once, which is what makes a container transform the right shape
// here rather than a per-marker hunt.
//
// The map from the clean-rotation projection the engine performs to the
// head-rotated one the scene was rendered with is, to an affine approximation,
// a rotation by head roll about the canvas centre plus a translation for
// yaw/pitch:
//
//   marker_final = R(roll) * (marker_native - C) + C + T
//
// which a single container transform expresses exactly as
//
//   main.Rotation = roll
//   main.Position = C - R(roll) * C + T
//
// The rotation sign follows the camera: head tracking rotates the camera basis
// by +roll about its forward axis, so a view tangent t maps to t' = Rz(roll)*t,
// and under the canvas mapping (x = -f*t.x + Cx, y = +f*t.y + Cy) that is a
// canvas-space rotation by +roll with the standard [[cos,-sin],[sin,cos]]
// matrix.
//
// T owns yaw/pitch alone: the marker tangents come from projecting the clean
// forward axis through the head basis, and roll leaves the forward axis fixed,
// so they collapse to ~0 under pure roll. The two terms are orthogonal.
//
// Translation parallax is not compensated: it is lean/depth and this is one
// transform for every marker at once, so no single value can be right for more
// than one of them. The marker tangents deliberately carry no lean term - the
// reticle's value is right for a marker sitting on the crosshair and wrong for
// every other one, badly wrong for a distant marker while the player aims at a
// near wall.
static void ApplyMarkerCompensation(reframework::API::ManagedObject* guiMo) {
    const auto& gui = ref::GetGuiMethods();
    if (!guiMo || !gui.ready || !gui.setRotation || !gui.viewGetScreenSize) return;

    const auto& crosshair = GetCrosshairProjection();
    if (!crosshair.valid || !ref::PluginMod::Instance().IsEnabled() || !IsInGameplay()) return;

    const float fovDeg = crosshair.fovDegrees;
    if (fovDeg < 10.f) return;

    // Canvas centre read from this GUI's own View. The rotation pivots on it, so
    // unlike the translation-only compensation that preceded this, a wrong
    // centre is not a scale error but a visible swing around the wrong point.
    float canvasW = 0.f, canvasH = 0.f;
    if (!ref::GetElementCanvasSize(guiMo, canvasW, canvasH)) return;

    const float centreX = canvasW * 0.5f;
    const float centreY = canvasH * 0.5f;
    float focalX = 0.f, focalY = 0.f;
    CanvasFocalLengths(canvasW, canvasH, focalX, focalY);

    const auto& projection = ref::GetFrameProjection();
    if (!projection.markerValid) return;
    const float offsetX = AimCanvasOffsetX(projection.markerTanRight, focalX);
    const float offsetY = AimCanvasOffsetY(projection.markerTanUp, focalY);

    const float rollDeg = crosshair.rollDegrees;
    const float rollRad = rollDeg * ref::kDegToRad;
    const float cosR = cosf(rollRad);
    const float sinR = sinf(rollRad);

    const float deltaX = centreX - (centreX * cosR - centreY * sinR) + offsetX;
    const float deltaY = centreY - (centreX * sinR + centreY * cosR) + offsetY;

    // Deliberately unsmoothed. Every input is already smoothed upstream (the
    // tracking pipeline for roll, the projection smoothing for the tangents and
    // FOV), and smoothing the translation while the rotation went through
    // unsmoothed would put the pivot correction out of phase with the rotation
    // it is correcting for - markers would swing on every roll change.

    uint32_t count = 0;
    auto arr = ref::FindPlayObjects(guiMo, count);
    if (!arr || count < 2) return;

    auto child1 = ref::ArrayGetValue(arr, 1);
    if (!child1) return;

    ref::SetTransformPosition(child1, deltaX, deltaY);

    float rot[3] = { 0.f, 0.f, rollDeg };
    ref::InvokeMethodWithArg(gui.setRotation, child1, (void*)&rot[0]);

    // Capped: the 120-frame interval alone streams for the whole session,
    // which buries the startup chain a user is asked to send.
    static int s_markerDiagFrame = 0;
    static int s_markerDiagLeft = 5;
    if (s_markerDiagLeft > 0 && (s_markerDiagFrame++ % 120) == 0) {
        s_markerDiagLeft--;
        float readBack = 0.f;
        if (gui.getRotation) {
            auto rotRet = gui.getRotation->invoke(child1, ref::EmptyArgs());
            if (!rotRet.exception_thrown) readBack = *reinterpret_cast<float*>(&rotRet.bytes[8]);
        }
        ref::LogInfo(
            "Marker comp: canvas=(%.0fx%.0f) centre=(%.1f,%.1f) focal=(%.1f,%.1f) roll=%.1f "
            "tanR=%.4f tanU=%.4f offset=(%.1f,%.1f) delta=(%.1f,%.1f) rot.z readback=%.1f",
            canvasW, canvasH, centreX, centreY, focalX, focalY, rollDeg,
            projection.markerTanRight, projection.markerTanUp, offsetX, offsetY,
            deltaX, deltaY, readBack);
    }
}

// --- Main dispatcher ---

bool OnPreGuiDrawElement(void* element, void* context) {
    (void)context;
    if (!element) return true;

    auto mo = reinterpret_cast<reframework::API::ManagedObject*>(element);

    char goName[128] = "?";
    ref::ReadGuiElementName(mo, goName, sizeof(goName));

    if (strncmp(goName, "Gui_ui20", 8) == 0) {
        LogGuiIdentity(mo, goName);
    }

    // MARKER COMPENSATION
    if (strncmp(goName, "Gui_ui2010", 10) == 0) {
        ApplyMarkerCompensation(mo);
    }

    // CROSSHAIR COMPENSATION
    // Gui_ui2020 alone. LogGuiIdentity confirms it is the reticle: 71 objects,
    // child[2] "layout", sitting natively at the canvas centre (960,540) on a
    // 1920x1080 canvas. Every other Gui_ui20* is screen-anchored HUD and stays
    // where the game puts it - Gui_ui2000's layout at (960,960), Gui_ui2050's
    // item list at (0,0), and Gui_ui2021, whose child[2] is a "gauge" at (0,0)
    // despite the "secondary crosshair element" it was long labelled.
    if (strncmp(goName, "Gui_ui2020", 10) == 0) {
        ApplyCrosshairOffset(mo, goName);
    }

    return true;
}

} // namespace RE9HT
