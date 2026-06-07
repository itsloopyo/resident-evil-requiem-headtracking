#include "pch.h"
#include "gui_compensation.h"
#include "gui_diagnostics.h"
#include "camera_internal.h"
#include "game_state_detector.h"
#include "core/mod.h"
#include "core/logger.h"

#include <cameraunlock/math/smoothing_utils.h>
#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/reframework/re_math.h>
#include <cameraunlock/rendering/gui_marker_compensation.h>

#include <reframework/API.hpp>
#include <unordered_set>
#include <string>
#include <cmath>

namespace RE9HT {

namespace ref = cameraunlock::reframework;

// GUI method cache — only the live methods needed for compensation.
static struct {
    reframework::API::ManagedObject* playObjectRuntimeType = nullptr;
    reframework::API::Method* guiFindObjectsByType = nullptr;
    reframework::API::Method* transformSetPosition = nullptr;
    reframework::API::Method* transformGetPosition = nullptr;
    reframework::API::Method* transformSetScale    = nullptr;
    reframework::API::Method* transformGetScale    = nullptr;
    reframework::API::Method* transformGetGlobalPosition = nullptr;
} g_guiMethods;

void InitGUICompensationMethods() {
    const auto& api = reframework::API::get();

    g_guiMethods.playObjectRuntimeType = api->typeof("via.gui.PlayObject");

    g_guiMethods.transformSetPosition = ref::FindMethodByParamCount("via.gui.TransformObject", "set_Position", 1);
    g_guiMethods.transformGetPosition = ref::FindMethodByParamCount("via.gui.TransformObject", "get_Position", 0);
    g_guiMethods.transformSetScale    = ref::FindMethodByParamCount("via.gui.TransformObject", "set_Scale", 1);
    g_guiMethods.transformGetScale    = ref::FindMethodByParamCount("via.gui.TransformObject", "get_Scale", 0);
    g_guiMethods.transformGetGlobalPosition = ref::FindMethodByParamCount("via.gui.TransformObject", "get_GlobalPosition", 0);

    // via.gui.GUI.findObjects — the 1-arg overload taking a System.Type.
    g_guiMethods.guiFindObjectsByType = ref::FindMethodByParamTypeName("via.gui.GUI", "findObjects", "Type");

    Logger::Instance().Info("GUI compensation methods: playObjType=%p findObjects(Type)=%p setPos=%p getGlobalPos=%p",
        (void*)g_guiMethods.playObjectRuntimeType,
        (void*)g_guiMethods.guiFindObjectsByType,
        (void*)g_guiMethods.transformSetPosition,
        (void*)g_guiMethods.transformGetGlobalPosition);
}

// --- FOV helpers ---

static bool GetMarkerProjectionFocalLengths(float& fx, float& fy) {
    fx = 0.f;
    fy = 0.f;
    constexpr float kHalfW = 960.f;
    constexpr float kHalfH = 540.f;

    float fov = CameraResolver().ResolveFovDegrees();
    if (!cameraunlock::rendering::FocalLengthsFromVerticalFov(fov, kHalfW, kHalfH, fx, fy)) {
        return false;
    }

    static bool s_fallbackLogged = false;
    if (!s_fallbackLogged) {
        Logger::Instance().Info("Marker focal lengths: assuming get_FOV %.1f is vertical -> fx=%.1f fy=%.1f",
            fov, fx, fy);
        s_fallbackLogged = true;
    }
    return true;
}

// --- Crosshair compensation ---

static void ApplyCrosshairOffset(reframework::API::ManagedObject* guiMo) {
    if (!guiMo || !g_guiMethods.guiFindObjectsByType || !g_guiMethods.playObjectRuntimeType
        || !g_guiMethods.transformSetPosition) {
        return;
    }
    if (!g_crosshair.valid || !Mod::Instance().IsEnabled() || !IsInGameplay()) return;

    float fovRad = g_crosshair.fovDegrees * DEG_TO_RAD;
    float tanHalfFovY = tanf(fovRad * 0.5f);
    constexpr float kCanvasW = 1920.0f;
    constexpr float kCanvasH = 1080.0f;
    float aspect = kCanvasW / kCanvasH;
    float tanHalfFovX = tanHalfFovY * aspect;

    float deltaX = -(g_crosshair.tanRight / tanHalfFovX) * (kCanvasW * 0.5f);
    float deltaY = (g_crosshair.tanUp / tanHalfFovY) * (kCanvasH * 0.5f);

    // Count descendants to distinguish small elements (crosshair) from large HUD containers.
    uint32_t descendantCount = 0;
    {
        std::vector<void*> findArgs = { (void*)g_guiMethods.playObjectRuntimeType };
        auto arrRet = g_guiMethods.guiFindObjectsByType->invoke(guiMo, findArgs);
        if (!arrRet.exception_thrown && arrRet.ptr) {
            auto arr = reinterpret_cast<reframework::API::ManagedObject*>(arrRet.ptr);
            auto lenRet = arr->invoke("get_Length", ref::EmptyArgs());
            if (!lenRet.exception_thrown) descendantCount = lenRet.dword;
        }
    }

    {
        static int s_diagFrame = 0;
        if ((s_diagFrame++ % 120) == 0) {
            Logger::Instance().Info("CROSSHAIR ApplyCrosshairOffset: descendants=%u deltaX=%.1f deltaY=%.1f",
                descendantCount, deltaX, deltaY);
        }
    }

    float pos[3] = { deltaX, deltaY, 0.f };
    std::vector<void*> setArgs = { (void*)&pos[0] };

    if (descendantCount > 100) {
        // LARGE ELEMENT: iterate View children, apply roll rotation if needed.
        auto viewRet = guiMo->invoke("get_View", ref::EmptyArgs());
        if (viewRet.exception_thrown || !viewRet.ptr) return;
        auto view = reinterpret_cast<reframework::API::ManagedObject*>(viewRet.ptr);

        auto childrenRet = view->invoke("getChildren", ref::EmptyArgs());
        if (childrenRet.exception_thrown || !childrenRet.ptr) return;
        auto childArr = reinterpret_cast<reframework::API::ManagedObject*>(childrenRet.ptr);
        auto lenRet = childArr->invoke("get_Length", ref::EmptyArgs());
        uint32_t count = lenRet.exception_thrown ? 0 : lenRet.dword;

        float absRoll = fabsf(g_crosshair.rollDegrees);
        bool applyRoll = (absRoll > 0.1f) && g_guiMethods.transformGetGlobalPosition;

        uint32_t cap = count < 64 ? count : 64;
        if (applyRoll) {
            float rollRad = g_crosshair.rollDegrees * DEG_TO_RAD;
            float cosR = cosf(rollRad);
            float sinR = sinf(rollRad);
            float zeroPos[3] = { 0.f, 0.f, 0.f };
            std::vector<void*> zeroArgs = { (void*)&zeroPos[0] };

            for (uint32_t i = 0; i < cap; i++) {
                auto elem = ref::ArrayGetValue(childArr, (int)i);
                if (!elem) continue;

                g_guiMethods.transformSetPosition->invoke(elem, zeroArgs);
                auto gpRet = g_guiMethods.transformGetGlobalPosition->invoke(elem, ref::EmptyArgs());
                if (gpRet.exception_thrown) continue;

                float gx = *reinterpret_cast<float*>(&gpRet.bytes[0]);
                float gy = *reinterpret_cast<float*>(&gpRet.bytes[4]);

                float rotX = gx * cosR - gy * sinR;
                float rotY = gx * sinR + gy * cosR;

                float finalPos[3] = { (rotX - gx) + deltaX, (rotY - gy) + deltaY, 0.f };
                std::vector<void*> finalArgs = { (void*)&finalPos[0] };
                g_guiMethods.transformSetPosition->invoke(elem, finalArgs);
            }
        } else {
            for (uint32_t i = 0; i < cap; i++) {
                auto elem = ref::ArrayGetValue(childArr, (int)i);
                if (!elem) continue;
                g_guiMethods.transformSetPosition->invoke(elem, setArgs);
            }
        }
    } else {
        // CROSSHAIR ELEMENT: target child[2] "layout" at baseline Position=(960,540,0).
        constexpr uint32_t kLayoutChildIdx = 2;
        std::vector<void*> findArgs = { (void*)g_guiMethods.playObjectRuntimeType };
        auto arrRet = g_guiMethods.guiFindObjectsByType->invoke(guiMo, findArgs);
        if (arrRet.exception_thrown || !arrRet.ptr) return;
        auto arr = reinterpret_cast<reframework::API::ManagedObject*>(arrRet.ptr);
        auto lenRet = arr->invoke("get_Length", ref::EmptyArgs());
        if (lenRet.exception_thrown || lenRet.dword <= kLayoutChildIdx) return;

        auto layoutElem = ref::ArrayGetValue(arr, (int)kLayoutChildIdx);
        if (!layoutElem) return;

        float absPos[3] = { 960.0f + deltaX, 540.0f + deltaY, 0.f };
        std::vector<void*> absArgs = { (void*)&absPos[0] };
        g_guiMethods.transformSetPosition->invoke(layoutElem, absArgs);

        static int s_verifyFrame = 0;
        if ((s_verifyFrame++ % 120) == 0 && g_guiMethods.transformGetGlobalPosition) {
            auto gpCheck = g_guiMethods.transformGetGlobalPosition->invoke(layoutElem, ref::EmptyArgs());
            if (!gpCheck.exception_thrown) {
                float rx = *reinterpret_cast<float*>(&gpCheck.bytes[0]);
                float ry = *reinterpret_cast<float*>(&gpCheck.bytes[4]);
                Logger::Instance().Info("CROSSHAIR layout[2]: wrote=(%.1f,%.1f) readback=(%.1f,%.1f)",
                    absPos[0], absPos[1], rx, ry);
            }
        }
    }
}

// --- Marker compensation ---

// Decomposition: marker_final = R_2d(roll) · (marker_native + rotation_offset)
//
// Translation parallax is *not* compensated here. OnPostBeginRendering
// restores clean rotation but keeps the head-tracked position, so at GUI
// draw time the camera matrix is (clean rotation, head position). Anything
// the GUI projects through that matrix already accounts for head
// translation — the world anchor's screen position naturally shifts with
// the lean, matching where the rendered scene shows the target. Adding a
// translation contribution here would double-compensate.
//
// What we *do* need to compensate is rotation, because the rotation was
// reset to clean in OnPostBeginRendering. g_marker.tanRight / tanUp is
// computed by projecting clean.fwd through the head-rotated basis without
// any head-position contribution, so it carries pure rotation parallax.
// Roll is baked into the head basis (q = Ry · Rx · Rz in ApplyHeadTracking)
// so the offset already encodes it; we then rotate the native marker
// position by the same roll so both terms share the roll factor.
//
// Note: this differs from Subnautica/Unity siblings (CanvasCompensation.cs),
// where roll is *not* baked into the camera projection — there the offset is
// computed with roll=0 and the rotation is applied separately to the marker.
// Here roll IS in the camera matrix so the offset already carries it.
static void ApplyMarkerCompensation(reframework::API::ManagedObject* guiMo) {
    if (!guiMo || !g_guiMethods.guiFindObjectsByType || !g_guiMethods.playObjectRuntimeType
        || !g_guiMethods.transformSetPosition || !g_guiMethods.transformGetGlobalPosition) {
        return;
    }
    if (!g_crosshair.valid || !g_marker.valid || !Mod::Instance().IsEnabled() || !IsInGameplay()) return;

    float fx = 0.f, fy = 0.f;
    if (!GetMarkerProjectionFocalLengths(fx, fy)) return;
    const float fovDeg = g_crosshair.fovDegrees;
    if (fovDeg < 10.f) return;
    const float tanHFovY = tanf(fovDeg * DEG_TO_RAD * 0.5f);
    constexpr float kHalfW_ = 960.f;
    constexpr float kHalfH_ = 540.f;
    const float aspect_ = kHalfW_ / kHalfH_;
    const float tanHFovX = tanHFovY * aspect_;

    // Resolve child[1].
    std::vector<void*> findArgs = { (void*)g_guiMethods.playObjectRuntimeType };
    auto arrRet = g_guiMethods.guiFindObjectsByType->invoke(guiMo, findArgs);
    if (arrRet.exception_thrown || !arrRet.ptr) return;
    auto arr = reinterpret_cast<reframework::API::ManagedObject*>(arrRet.ptr);
    auto lenRet = arr->invoke("get_Length", ref::EmptyArgs());
    if (lenRet.exception_thrown || lenRet.dword < 2) return;

    auto child1 = ref::ArrayGetValue(arr, 1);
    if (!child1) return;

    float zeroPos[3] = { 0.f, 0.f, 0.f };
    std::vector<void*> zeroArgs = { (void*)&zeroPos[0] };
    g_guiMethods.transformSetPosition->invoke(child1, zeroArgs);

    static int s_markerDiagFrame = 0;
    bool markerDiag = ((s_markerDiagFrame++ % 120) == 0);

    float markerX = 0.f, markerY = 0.f;
    bool hasMarkerAnchor = false;

    constexpr uint32_t kMarkerAnchorCandidateIndex = 28;
    if (lenRet.dword > kMarkerAnchorCandidateIndex) {
        auto anchor = ref::ArrayGetValue(arr, (int)kMarkerAnchorCandidateIndex);
        if (anchor) {
            auto gpAnchor = g_guiMethods.transformGetGlobalPosition->invoke(anchor, ref::EmptyArgs());
            if (!gpAnchor.exception_thrown) {
                float ax = *reinterpret_cast<float*>(&gpAnchor.bytes[0]);
                float ay = *reinterpret_cast<float*>(&gpAnchor.bytes[4]);
                if (std::isfinite(ax) && std::isfinite(ay)
                    && fabsf(ax) <= 2400.f && fabsf(ay) <= 1600.f) {
                    markerX = ax;
                    markerY = ay;
                    hasMarkerAnchor = true;
                }
            }
        }
    }

    if (!hasMarkerAnchor) {
        auto gp = g_guiMethods.transformGetGlobalPosition->invoke(child1, ref::EmptyArgs());
        if (!gp.exception_thrown) {
            markerX = *reinterpret_cast<float*>(&gp.bytes[0]);
            markerY = *reinterpret_cast<float*>(&gp.bytes[4]);
            hasMarkerAnchor = std::isfinite(markerX) && std::isfinite(markerY);
        }
    }

    // Marker compensation = yaw/pitch translation + a 2D roll rotation of the
    // anchor about screen centre, the two sharing one roll factor. This is the
    // same decomposition the large-HUD-element path applies to the rolled HUD
    // container (see ApplyCrosshairOffset), and it must use the same roll sign
    // so markers and HUD agree.
    //
    // Why not a single direction-space reprojection through the measured
    // clean-to-head rotation: for pure roll that reprojection collapses
    // (the FOV/aspect terms cancel) to a flat screen rotation by -roll, i.e.
    // the OPPOSITE sense to the rolled HUD. On an off-centre anchor — which is
    // exactly what moving the aim with the mouse produces — that counter-roll
    // reads as perpendicular drift: a vertical anchor offset wanders
    // horizontally, a horizontal offset wanders vertically. A flat screen
    // rotation matches how the engine rolls the 2D HUD, with no such drift.
    //
    // tanRight/tanUp from the forward projection carry the yaw/pitch screen
    // shift (they collapse to ~0 under pure roll, since the roll axis is the
    // view forward), so the roll rotation owns roll and the offset owns
    // yaw/pitch.
    const float offsetX = -g_marker.tanRight * fx;
    const float offsetY =  g_marker.tanUp * fy;

    const float rollR = g_crosshair.rollDegrees * DEG_TO_RAD;
    const float cosR = cosf(rollR);
    const float sinR = sinf(rollR);
    const float rotX = markerX * cosR - markerY * sinR;
    const float rotY = markerX * sinR + markerY * cosR;

    float deltaX = (rotX - markerX) + offsetX;
    float deltaY = (rotY - markerY) + offsetY;

    // Smooth marker delta to eliminate jitter from FOV fluctuations and
    // anchor readback variance.
    {
        static cameraunlock::math::SmoothedFloat s_markerDeltaX;
        static cameraunlock::math::SmoothedFloat s_markerDeltaY;
        constexpr float kSmoothing = static_cast<float>(cameraunlock::math::kBaselineSmoothing);
        float dt = Mod::Instance().GetLastDeltaTime();
        deltaX = s_markerDeltaX.Update(deltaX, kSmoothing, dt);
        deltaY = s_markerDeltaY.Update(deltaY, kSmoothing, dt);
    }

    if (markerDiag) {
        Logger::Instance().Info(
            "Marker comp: roll=%.1f anchor=(%.1f,%.1f) tanR=%.4f tanU=%.4f offset=(%.1f,%.1f) delta=(%.1f,%.1f)",
            g_crosshair.rollDegrees,
            markerX, markerY,
            g_marker.tanRight, g_marker.tanUp,
            offsetX, offsetY,
            deltaX, deltaY);
    }

    float pos[3] = { deltaX, deltaY, 0.f };
    std::vector<void*> setArgs = { (void*)&pos[0] };
    g_guiMethods.transformSetPosition->invoke(child1, setArgs);
}

// --- Main dispatcher ---

void ResetGuiElementDumper() {
    ResetGuiDiagnostics();
}

bool OnPreGuiDrawElement(void* element, void* context) {
    if (!element) return true;

    TryDumpContext(context);
    TryDumpMatrixDiagnostic();

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

    // Diagnostic scans
    ScanGuiGoName(goName, tns, tnm);
    TryDumpGuiElement(mo, td, goName, goMo);

    // MARKER COMPENSATION
    if (strncmp(goName, "Gui_ui2010", 10) == 0) {
        ApplyMarkerCompensation(mo);
    }

    // CROSSHAIR COMPENSATION
    bool isCrosshairCandidate = (strncmp(goName, "Gui_ui20", 8) == 0)
                             && (strncmp(goName, "Gui_ui2010", 10) != 0);
    if (isCrosshairCandidate && g_crosshair.valid) {
        static std::unordered_set<std::string> s_loggedCrosshairGOs;
        if (s_loggedCrosshairGOs.insert(std::string(goName)).second) {
            Logger::Instance().Info("Crosshair offset target: GO=\"%s\"", goName);
        }
        ApplyCrosshairOffset(mo);
    }

    // HIDE GATE
    if (Mod::Instance().AreMarkersHidden()) {
        return false;
    }
    return true;
}

} // namespace RE9HT
