#include "pch.h"
#include "gui_compensation.h"
#include "gui_diagnostics.h"
#include "camera_internal.h"
#include "game_state_detector.h"
#include "core/mod.h"
#include "core/logger.h"

#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/reframework/re_math.h>
#include <cameraunlock/math/smoothing_utils.h>

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
    auto tdb = api->tdb();

    g_guiMethods.playObjectRuntimeType = api->typeof("via.gui.PlayObject");

    g_guiMethods.transformSetPosition = ref::FindMethodByParamCount("via.gui.TransformObject", "set_Position", 1);
    g_guiMethods.transformGetPosition = ref::FindMethodByParamCount("via.gui.TransformObject", "get_Position", 0);
    g_guiMethods.transformSetScale    = ref::FindMethodByParamCount("via.gui.TransformObject", "set_Scale", 1);
    g_guiMethods.transformGetScale    = ref::FindMethodByParamCount("via.gui.TransformObject", "get_Scale", 0);
    g_guiMethods.transformGetGlobalPosition = ref::FindMethodByParamCount("via.gui.TransformObject", "get_GlobalPosition", 0);

    // via.gui.GUI.findObjects — find the 1-arg overload taking a System.Type.
    auto guiType = tdb->find_type("via.gui.GUI");
    if (guiType) {
        for (auto m : guiType->get_methods()) {
            if (!m) continue;
            const char* name = m->get_name();
            if (!name || strcmp(name, "findObjects") != 0) continue;
            if (m->get_num_params() != 1) continue;
            auto params = m->get_params();
            if (params.size() == 1 && params[0].t) {
                auto pt = reinterpret_cast<reframework::API::TypeDefinition*>(params[0].t);
                if (pt && pt->get_name() && strcmp(pt->get_name(), "Type") == 0) {
                    g_guiMethods.guiFindObjectsByType = m;
                    break;
                }
            }
        }
    }

    Logger::Instance().Info("GUI compensation methods: playObjType=%p findObjects(Type)=%p setPos=%p getGlobalPos=%p",
        (void*)g_guiMethods.playObjectRuntimeType,
        (void*)g_guiMethods.guiFindObjectsByType,
        (void*)g_guiMethods.transformSetPosition,
        (void*)g_guiMethods.transformGetGlobalPosition);
}

// --- FOV helpers ---

static float GetLivePrimaryCameraFov() {
    // Delegate to the camera_hook's cached methods via the shared API
    // This is a simplified version that reads FOV through the standard chain.
    static bool s_diagLogged = false;
    static reframework::API::Method* s_getMainView = nullptr;
    static reframework::API::Method* s_getPrimaryCamera = nullptr;
    static reframework::API::Method* s_getCameraFov = nullptr;
    static bool s_initialized = false;

    if (!s_initialized) {
        s_initialized = true;
        const auto& api = reframework::API::get();
        auto tdb = api->tdb();
        auto smType = tdb->find_type("via.SceneManager");
        auto svType = tdb->find_type("via.SceneView");
        auto camType = tdb->find_type("via.Camera");
        if (smType) s_getMainView = smType->find_method("get_MainView");
        if (svType) s_getPrimaryCamera = svType->find_method("get_PrimaryCamera");
        if (camType) s_getCameraFov = camType->find_method("get_FOV");
    }

    if (!s_getMainView || !s_getPrimaryCamera || !s_getCameraFov) return 0.f;

    const auto& api = reframework::API::get();
    void* sm = api->get_native_singleton("via.SceneManager");
    if (!sm) return 0.f;

    auto mv = s_getMainView->invoke(
        reinterpret_cast<reframework::API::ManagedObject*>(sm), ref::EmptyArgs());
    if (mv.exception_thrown || !mv.ptr) return 0.f;

    auto cam = s_getPrimaryCamera->invoke(
        reinterpret_cast<reframework::API::ManagedObject*>(mv.ptr), ref::EmptyArgs());
    if (cam.exception_thrown || !cam.ptr) return 0.f;

    if (!s_diagLogged) {
        auto camMo = reinterpret_cast<reframework::API::ManagedObject*>(cam.ptr);
        auto td = camMo->get_type_definition();
        const char* tns = (td && td->get_namespace()) ? td->get_namespace() : "";
        const char* tnm = (td && td->get_name()) ? td->get_name() : "?";
        Logger::Instance().Info("GetLivePrimaryCameraFov: primary camera type = %s.%s", tns, tnm);
    }

    auto fov = s_getCameraFov->invoke(
        reinterpret_cast<reframework::API::ManagedObject*>(cam.ptr), ref::EmptyArgs());
    if (fov.exception_thrown) return 0.f;

    float fovDeg = 0.f;
    if (fov.f >= 10.f && fov.f <= 170.f) fovDeg = fov.f;
    else { float fromD = static_cast<float>(fov.d); if (fromD >= 10.f && fromD <= 170.f) fovDeg = fromD; }

    if (!s_diagLogged) {
        Logger::Instance().Info("GetLivePrimaryCameraFov: raw f=%.4f d=%.4f -> chose %.4f", fov.f, fov.d, fovDeg);
        s_diagLogged = true;
    }

    return fovDeg;
}

static bool GetMarkerProjectionFocalLengths(float& fx, float& fy) {
    fx = 0.f;
    fy = 0.f;
    constexpr float kHalfW = 960.f;
    constexpr float kHalfH = 540.f;
    constexpr float kAspect = kHalfW / kHalfH;

    float fov = GetLivePrimaryCameraFov();
    if (fov < 10.f || fov > 170.f) return false;

    static bool s_fallbackLogged = false;
    float tanHFovY = tanf(fov * DEG_TO_RAD * 0.5f);
    float tanHFovX = tanHFovY * kAspect;
    fx = kHalfW / tanHFovX;
    fy = kHalfH / tanHFovY;
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

    // Direction-space marker compensation. The "rotate anchor + add forward
    // offset" approach works when the anchor is at screen center (offset
    // alone is correct) or under pure roll (rotation alone is correct), but
    // breaks for off-center anchors under combined yaw/pitch/roll because
    // the "forward offset" assumes a uniform screen shift that's only valid
    // for the forward-aim direction. The proper transform: convert anchor
    // to a direction in clean-camera-local frame, apply the inverse head-
    // tracking rotation, project back. Subsumes yaw/pitch translation and
    // roll rotation in one calculation, exact for any anchor position.
    //
    // Anchor (markerX, markerY) is in canvas-center-origin, +X right, +Y up.
    // Convert to NDC, then to direction in clean-camera-local: (a, b, 1).
    float dirX = (markerX / kHalfW_) * tanHFovX;
    float dirY = (markerY / kHalfH_) * tanHFovY;
    float dirZ = 1.0f;

    // R_track in ApplyHeadTracking = R_y(yr) * R_x(pr) * R_z(rr) where
    // yr = -yaw, pr = pitch, rr = roll. So R_track^T = R_z(-rr) * R_x(-pr)
    // * R_y(-yr) = R_z(-roll) * R_x(-pitch) * R_y(yaw). To apply
    // R_track^T to a vector, multiply right-to-left: R_y(yaw) first, then
    // R_x(-pitch), then R_z(-roll).
    float yawDeg = 0.f, pitchDeg = 0.f, rollDeg = 0.f;
    Mod::Instance().GetProcessedRotation(yawDeg, pitchDeg, rollDeg);
    const float yawRad   = -yawDeg   * DEG_TO_RAD;
    const float pitchRad =  pitchDeg * DEG_TO_RAD;
    const float rollRad  = -rollDeg  * DEG_TO_RAD;

    // Apply R_y(yaw): x' = x cos + z sin, z' = -x sin + z cos
    {
        const float c = cosf(yawRad), s = sinf(yawRad);
        const float nx = dirX * c + dirZ * s;
        const float nz = -dirX * s + dirZ * c;
        dirX = nx; dirZ = nz;
    }
    // Apply R_x(-pitch): y' = y cos + z sin, z' = -y sin + z cos
    {
        const float c = cosf(pitchRad), s = sinf(pitchRad);
        const float ny = dirY * c + dirZ * s;
        const float nz = -dirY * s + dirZ * c;
        dirY = ny; dirZ = nz;
    }
    // Apply R_z(-roll): x' = x cos + y sin, y' = -x sin + y cos
    {
        const float c = cosf(rollRad), s = sinf(rollRad);
        const float nx = dirX * c + dirY * s;
        const float ny = -dirX * s + dirY * c;
        dirX = nx; dirY = ny;
    }

    if (dirZ < 1e-4f) {
        // Direction folded behind camera; skip compensation this frame.
        return;
    }

    const float newCanvasX = (dirX / dirZ / tanHFovX) * kHalfW_;
    const float newCanvasY = (dirY / dirZ / tanHFovY) * kHalfH_;

    float deltaX = newCanvasX - markerX;
    float deltaY = newCanvasY - markerY;

    // Unused under direction-space transform; kept defined for diagnostic.
    const float offsetX = -g_marker.tanRight * fx;
    const float offsetY =  g_marker.tanUp * fy;

    // Smooth marker delta to eliminate jitter from FOV fluctuations and
    // anchor readback variance.
    {
        static float s_markerDeltaX = 0.f;
        static float s_markerDeltaY = 0.f;
        static bool s_markerSmoothedInit = false;
        constexpr float kSmoothing = static_cast<float>(cameraunlock::math::kBaselineSmoothing);
        float dt = Mod::Instance().GetLastDeltaTime();
        float t = cameraunlock::math::CalculateSmoothingFactor(kSmoothing, dt);
        if (!s_markerSmoothedInit) {
            s_markerDeltaX = deltaX;
            s_markerDeltaY = deltaY;
            s_markerSmoothedInit = true;
        } else {
            s_markerDeltaX = cameraunlock::math::Lerp(s_markerDeltaX, deltaX, t);
            s_markerDeltaY = cameraunlock::math::Lerp(s_markerDeltaY, deltaY, t);
        }
        deltaX = s_markerDeltaX;
        deltaY = s_markerDeltaY;
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
