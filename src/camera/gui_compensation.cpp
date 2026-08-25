#include "pch.h"
#include "gui_compensation.h"
#include "camera_internal.h"
#include "game_state_detector.h"
#include "core/mod.h"
#include "core/logger.h"

#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/reframework/re_math.h>

#include <reframework/API.hpp>
#include <unordered_set>
#include <string>
#include <cmath>

namespace RE9HT {

namespace ref = cameraunlock::reframework;

// Cap on unique GUI GameObject names recorded during identity logging.
constexpr size_t kMaxLoggedGuiIdentities = 100;

// "layout" - the node both the reticle write and the identity probe target.
constexpr uint32_t kLayoutChildIdx = 2;

// GUI method cache - only the live methods needed for compensation.
static struct {
    reframework::API::ManagedObject* playObjectRuntimeType = nullptr;
    reframework::API::Method* guiFindObjectsByType = nullptr;
    reframework::API::Method* transformSetPosition = nullptr;
    reframework::API::Method* transformGetPosition = nullptr;
    reframework::API::Method* transformSetRotation = nullptr;
    reframework::API::Method* transformGetRotation = nullptr;
    reframework::API::Method* viewGetScreenSize    = nullptr;
} g_guiMethods;

void InitGUICompensationMethods() {
    const auto& api = reframework::API::get();

    g_guiMethods.playObjectRuntimeType = api->typeof("via.gui.PlayObject");

    g_guiMethods.transformSetPosition = ref::FindMethodByParamCount("via.gui.TransformObject", "set_Position", 1);
    g_guiMethods.transformGetPosition = ref::FindMethodByParamCount("via.gui.TransformObject", "get_Position", 0);
    g_guiMethods.transformSetRotation = ref::FindMethodByParamCount("via.gui.TransformObject", "set_Rotation", 1);
    g_guiMethods.transformGetRotation = ref::FindMethodByParamCount("via.gui.TransformObject", "get_Rotation", 0);

    // The canvas centre the marker roll term rotates about. Read per GUI from
    // its own View rather than assuming 1920x1080.
    g_guiMethods.viewGetScreenSize = ref::FindMethodByParamCount("via.gui.View", "get_ScreenSize", 0);

    // via.gui.GUI.findObjects - the 1-arg overload taking a System.Type.
    g_guiMethods.guiFindObjectsByType = ref::FindMethodByParamTypeName("via.gui.GUI", "findObjects", "Type");

    Logger::Instance().Info("GUI compensation methods: playObjType=%p findObjects(Type)=%p setPos=%p "
        "setRot=%p viewGetScreenSize=%p",
        (void*)g_guiMethods.playObjectRuntimeType,
        (void*)g_guiMethods.guiFindObjectsByType,
        (void*)g_guiMethods.transformSetPosition,
        (void*)g_guiMethods.transformSetRotation,
        (void*)g_guiMethods.viewGetScreenSize);
}

// --- Canvas geometry ---

// The GUI canvas this element is laid out on, read from its own View. Every
// screen-space constant in this file used to be hardcoded to 1920x1080, which
// silently mislocates the crosshair and the marker rotation pivot on any other
// canvas. Each GUI carries its own size, so this is per element, not global.
static bool GetCanvasSize(reframework::API::ManagedObject* guiMo, float& canvasW, float& canvasH) {
    if (!g_guiMethods.viewGetScreenSize) return false;

    auto viewRet = guiMo->invoke("get_View", ref::EmptyArgs());
    if (viewRet.exception_thrown || !viewRet.ptr) return false;
    auto view = reinterpret_cast<reframework::API::ManagedObject*>(viewRet.ptr);
    auto sizeRet = g_guiMethods.viewGetScreenSize->invoke(view, ref::EmptyArgs());
    if (sizeRet.exception_thrown) return false;

    canvasW = *reinterpret_cast<float*>(&sizeRet.bytes[0]);
    canvasH = *reinterpret_cast<float*>(&sizeRet.bytes[4]);
    return canvasW > 100.f && canvasW < 16384.f && canvasH > 100.f && canvasH < 16384.f;
}

// Pixel focal lengths, from the camera's own projection matrix. Scaling the NDC
// factor against half the canvas in each axis carries the aspect ratio the
// engine actually renders with, so nothing here has to assume whether get_FOV
// means the vertical or the horizontal angle - see CrosshairProjection.
static void CanvasFocalLengths(float canvasW, float canvasH,
                               float& focalX, float& focalY) {
    focalX = g_crosshair.ndcPerTanX * canvasW * 0.5f;
    focalY = g_crosshair.ndcPerTanY * canvasH * 0.5f;
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
    if (!g_guiMethods.guiFindObjectsByType || !g_guiMethods.playObjectRuntimeType
        || !g_guiMethods.transformGetPosition) {
        return;
    }

    static std::unordered_set<std::string> s_logged;
    if (s_logged.size() >= kMaxLoggedGuiIdentities) return;
    if (!s_logged.insert(std::string(goName)).second) return;

    float canvasW = 0.f, canvasH = 0.f;
    if (!GetCanvasSize(guiMo, canvasW, canvasH)) return;

    std::vector<void*> findArgs = { (void*)g_guiMethods.playObjectRuntimeType };
    auto arrRet = g_guiMethods.guiFindObjectsByType->invoke(guiMo, findArgs);
    if (arrRet.exception_thrown || !arrRet.ptr) return;
    auto arr = reinterpret_cast<reframework::API::ManagedObject*>(arrRet.ptr);
    auto lenRet = arr->invoke("get_Length", ref::EmptyArgs());
    if (lenRet.exception_thrown || lenRet.dword <= kLayoutChildIdx) return;

    auto layout = ref::ArrayGetValue(arr, (int)kLayoutChildIdx);
    if (!layout) return;

    char childName[64] = "?";
    auto nameRet = layout->invoke("get_Name", ref::EmptyArgs());
    if (!nameRet.exception_thrown && nameRet.ptr) {
        ref::ReadManagedString(nameRet.ptr, childName, sizeof(childName));
    }

    auto posRet = g_guiMethods.transformGetPosition->invoke(layout, ref::EmptyArgs());
    if (posRet.exception_thrown) return;
    float px = *reinterpret_cast<float*>(&posRet.bytes[0]);
    float py = *reinterpret_cast<float*>(&posRet.bytes[4]);

    bool atCentre = fabsf(px - canvasW * 0.5f) < 1.f && fabsf(py - canvasH * 0.5f) < 1.f;
    Logger::Instance().Info(
        "GUI identity: \"%s\" canvas=(%.0fx%.0f) objects=%u child[%u]=\"%s\" nativePos=(%.1f,%.1f)%s",
        goName, canvasW, canvasH, lenRet.dword, kLayoutChildIdx, childName, px, py,
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
// Only ever called for a reticle GUI. It used to be called for every Gui_ui20*
// except the marker GUI, which in practice meant Gui_ui2000 and Gui_ui2050 -
// the HUD and the item list - and never a reticle, since no reticle GUI appears
// in any captured log. Screen-anchored HUD needs no compensation at all: it is
// drawn in screen space, so head rotation does not move what it labels.
//
// The absolute write is canvas centre plus the aim offset, with no baseline
// capture, because a reticle marks the aim direction and that direction
// projects to the canvas centre in the clean view. A GUI whose layout node is
// not natively at the centre is not a reticle, which is what LogGuiIdentity
// records.
static void ApplyCrosshairOffset(reframework::API::ManagedObject* guiMo, const char* goName) {
    if (!guiMo || !g_guiMethods.guiFindObjectsByType || !g_guiMethods.playObjectRuntimeType
        || !g_guiMethods.transformSetPosition) {
        return;
    }
    if (!g_crosshair.valid || !Mod::Instance().IsEnabled() || !IsInGameplay()) return;

    float canvasW = 0.f, canvasH = 0.f;
    if (!GetCanvasSize(guiMo, canvasW, canvasH)) return;
    const float centreX = canvasW * 0.5f;
    const float centreY = canvasH * 0.5f;
    // NDC runs -1..+1 with y up; the canvas runs 0..height with y down.
    const float deltaX =  g_crosshair.ndcX * canvasW * 0.5f;
    const float deltaY = -g_crosshair.ndcY * canvasH * 0.5f;

    std::vector<void*> findArgs = { (void*)g_guiMethods.playObjectRuntimeType };
    auto arrRet = g_guiMethods.guiFindObjectsByType->invoke(guiMo, findArgs);
    if (arrRet.exception_thrown || !arrRet.ptr) return;
    auto arr = reinterpret_cast<reframework::API::ManagedObject*>(arrRet.ptr);
    auto lenRet = arr->invoke("get_Length", ref::EmptyArgs());
    if (lenRet.exception_thrown || lenRet.dword <= kLayoutChildIdx) return;

    auto layoutElem = ref::ArrayGetValue(arr, (int)kLayoutChildIdx);
    if (!layoutElem) return;

    // Where the reticle sat before this write. The absolute write below assumes
    // the game parks its reticle at the canvas centre and leaves it there, so
    // that centre plus our offset is the whole story. If the game moves it
    // itself - for weapon sway, recoil, or a sight whose convergence depends on
    // range - then overwriting it discards that and leaves an error the mod
    // cannot see. One read says which.
    float before[3] = { 0.f, 0.f, 0.f };
    {
        auto preRet = g_guiMethods.transformGetPosition->invoke(layoutElem, ref::EmptyArgs());
        if (!preRet.exception_thrown) {
            before[0] = *reinterpret_cast<float*>(&preRet.bytes[0]);
            before[1] = *reinterpret_cast<float*>(&preRet.bytes[4]);
        }
    }

    float absPos[3] = { centreX + deltaX, centreY + deltaY, 0.f };
    std::vector<void*> absArgs = { (void*)&absPos[0] };
    g_guiMethods.transformSetPosition->invoke(layoutElem, absArgs);

    // Capped: the 120-frame interval alone streams for the whole
    // session, which buries the startup chain a user is asked to send.
    static int s_diagFrame = 0;
    static int s_diagLeft = 5;
    if (s_diagLeft > 0 && (s_diagFrame++ % 120) == 0) {
        s_diagLeft--;
        Logger::Instance().Info("CROSSHAIR \"%s\": canvas=(%.0fx%.0f) centre=(%.1f,%.1f) "
            "ndc=(%.4f,%.4f) delta=(%.1f,%.1f) before=(%.1f,%.1f) wrote=(%.1f,%.1f)",
            goName, canvasW, canvasH, centreX, centreY,
            g_crosshair.ndcX, g_crosshair.ndcY, deltaX, deltaY,
            before[0], before[1], absPos[0], absPos[1]);
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
// The rotation sign follows the camera: ApplyHeadTracking rotates the camera
// basis by +roll about its forward axis, so a view tangent t maps to
// t' = Rz(roll) * t, and under the canvas mapping (x = -f*t.x + Cx,
// y = +f*t.y + Cy) that is a canvas-space rotation by +roll with the standard
// [[cos,-sin],[sin,cos]] matrix.
//
// T owns yaw/pitch alone: the aim tangents come from projecting the clean
// forward axis through the head basis, and roll leaves the forward axis fixed,
// so they collapse to ~0 under pure roll. The two terms are orthogonal.
//
// Translation parallax is not compensated: it is lean/depth and this is one
// transform for every marker at once, so no single value can be right for more
// than one of them. See MarkerProjection.
//
// What this replaces: the old roll term read PlayObject index 28 as the
// "marker's native screen position" and rotated it about the canvas ORIGIN.
// Index 28 is "bg_panel", a design-time layout panel whose global position is
// the constant (80,-46) on every frame regardless of where the marker is, so
// the term injected a fixed ~92px*roll shift that was identical for a marker at
// screen centre and one at the edge, and never applied the real correction,
// which scales with distance from the centre.
static void ApplyMarkerCompensation(reframework::API::ManagedObject* guiMo) {
    if (!guiMo || !g_guiMethods.guiFindObjectsByType || !g_guiMethods.playObjectRuntimeType
        || !g_guiMethods.transformSetPosition || !g_guiMethods.transformSetRotation
        || !g_guiMethods.viewGetScreenSize) {
        return;
    }
    if (!g_crosshair.valid || !Mod::Instance().IsEnabled() || !IsInGameplay()) return;

    const float fovDeg = g_crosshair.fovDegrees;
    if (fovDeg < 10.f) return;

    // Canvas centre read from this GUI's own View. The rotation pivots on it, so
    // unlike the translation-only compensation that preceded this, a wrong
    // centre is not a scale error but a visible swing around the wrong point.
    float canvasW = 0.f, canvasH = 0.f;
    if (!GetCanvasSize(guiMo, canvasW, canvasH)) return;

    const float centreX = canvasW * 0.5f;
    const float centreY = canvasH * 0.5f;
    float focalX = 0.f, focalY = 0.f;
    CanvasFocalLengths(canvasW, canvasH, focalX, focalY);

    // Rotation only. The reticle's tangents carry parallax at the distance the
    // gun is pointing, which is the wrong depth for every marker that is not
    // sitting on the crosshair - see MarkerProjection.
    if (!g_marker.valid) return;
    const float offsetX = AimCanvasOffsetX(g_marker.tanRight, focalX);
    const float offsetY = AimCanvasOffsetY(g_marker.tanUp, focalY);

    const float rollDeg = g_crosshair.rollDegrees;
    const float rollRad = rollDeg * DEG_TO_RAD;
    const float cosR = cosf(rollRad);
    const float sinR = sinf(rollRad);

    const float deltaX = centreX - (centreX * cosR - centreY * sinR) + offsetX;
    const float deltaY = centreY - (centreX * sinR + centreY * cosR) + offsetY;

    // Deliberately unsmoothed. Every input is already smoothed upstream (the
    // tracking pipeline for roll, kProjectionSmoothing for the tangents and
    // FOV), and smoothing the translation while the rotation went through
    // unsmoothed would put the pivot correction out of phase with the rotation
    // it is correcting for - markers would swing on every roll change.

    std::vector<void*> findArgs = { (void*)g_guiMethods.playObjectRuntimeType };
    auto arrRet = g_guiMethods.guiFindObjectsByType->invoke(guiMo, findArgs);
    if (arrRet.exception_thrown || !arrRet.ptr) return;
    auto arr = reinterpret_cast<reframework::API::ManagedObject*>(arrRet.ptr);
    auto lenRet = arr->invoke("get_Length", ref::EmptyArgs());
    if (lenRet.exception_thrown || lenRet.dword < 2) return;

    auto child1 = ref::ArrayGetValue(arr, 1);
    if (!child1) return;

    float pos[3] = { deltaX, deltaY, 0.f };
    std::vector<void*> posArgs = { (void*)&pos[0] };
    g_guiMethods.transformSetPosition->invoke(child1, posArgs);

    float rot[3] = { 0.f, 0.f, rollDeg };
    std::vector<void*> rotArgs = { (void*)&rot[0] };
    g_guiMethods.transformSetRotation->invoke(child1, rotArgs);

    // Capped: the 120-frame interval alone streams for the whole session,
    // which buries the startup chain a user is asked to send.
    static int s_markerDiagFrame = 0;
    static int s_markerDiagLeft = 5;
    if (s_markerDiagLeft > 0 && (s_markerDiagFrame++ % 120) == 0) {
        s_markerDiagLeft--;
        float readBack = 0.f;
        if (g_guiMethods.transformGetRotation) {
            auto rotRet = g_guiMethods.transformGetRotation->invoke(child1, ref::EmptyArgs());
            if (!rotRet.exception_thrown) readBack = *reinterpret_cast<float*>(&rotRet.bytes[8]);
        }
        Logger::Instance().Info(
            "Marker comp: canvas=(%.0fx%.0f) centre=(%.1f,%.1f) focal=(%.1f,%.1f) roll=%.1f "
            "tanR=%.4f tanU=%.4f offset=(%.1f,%.1f) delta=(%.1f,%.1f) rot.z readback=%.1f",
            canvasW, canvasH, centreX, centreY, focalX, focalY, rollDeg,
            g_marker.tanRight, g_marker.tanUp, offsetX, offsetY, deltaX, deltaY, readBack);
    }
}

// --- Main dispatcher ---

bool OnPreGuiDrawElement(void* element, void* context) {
    if (!element) return true;

    auto mo = reinterpret_cast<reframework::API::ManagedObject*>(element);
    auto td = mo->get_type_definition();
    if (!td) return true;
    const char* tns = td->get_namespace();
    const char* tnm = td->get_name();
    if (!tnm) return true;

    // Resolve the GameObject name
    char goName[128] = "?";
    reframework::API::ManagedObject* goMo = nullptr;
    auto goRet = mo->invoke("get_GameObject", ref::EmptyArgs());
    if (!goRet.exception_thrown && goRet.ptr) {
        goMo = reinterpret_cast<reframework::API::ManagedObject*>(goRet.ptr);
        auto nameRet = goMo->invoke("get_Name", ref::EmptyArgs());
        if (!nameRet.exception_thrown && nameRet.ptr) {
            ref::ReadManagedString(nameRet.ptr, goName, sizeof(goName));
        }
    }

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
    if (strncmp(goName, "Gui_ui2020", 10) == 0 && g_crosshair.valid) {
        ApplyCrosshairOffset(mo, goName);
    }

    return true;
}

} // namespace RE9HT
