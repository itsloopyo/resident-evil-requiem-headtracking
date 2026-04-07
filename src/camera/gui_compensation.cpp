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

static constexpr float kMarkerAssumedDepthMeters = 1.5f;

static bool ProjectCleanMarkerRayToHeadGui(float cleanX, float cleanY, float cleanZ,
                                           float fx, float fy, float& guiX, float& guiY) {
    if (!g_C_valid) return false;

    float rr = g_crosshair.rollDegrees * DEG_TO_RAD;
    float cr = cosf(rr), sr = sinf(rr);

    float C0[3], C1[3];
    for (int j = 0; j < 3; j++) {
        C0[j] = cr * g_C[0][j] - sr * g_C[1][j];
        C1[j] = sr * g_C[0][j] + cr * g_C[1][j];
    }

    float vx = C0[0] * cleanX + C0[1] * cleanY + C0[2] * cleanZ;
    float vy = C1[0] * cleanX + C1[1] * cleanY + C1[2] * cleanZ;
    float vz = g_C[2][0] * cleanX + g_C[2][1] * cleanY + g_C[2][2] * cleanZ;
    if (vz < 1e-4f) return false;

    guiX = -(vx / vz) * fx;
    guiY =  (vy / vz) * fy;
    return true;
}

static void ApplyMarkerCompensation(reframework::API::ManagedObject* guiMo) {
    if (!guiMo || !g_guiMethods.guiFindObjectsByType || !g_guiMethods.playObjectRuntimeType
        || !g_guiMethods.transformSetPosition || !g_guiMethods.transformGetGlobalPosition) {
        return;
    }
    if (!g_crosshair.valid || !Mod::Instance().IsEnabled() || !IsInGameplay()) return;

    float fx = 0.f, fy = 0.f;
    if (!GetMarkerProjectionFocalLengths(fx, fy)) return;

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

    float markerX = 0.f, markerY = 0.f, markerZ = 0.f;
    bool hasMarkerAnchor = false;

    constexpr uint32_t kMarkerAnchorCandidateIndex = 28;
    if (lenRet.dword > kMarkerAnchorCandidateIndex) {
        auto anchor = ref::ArrayGetValue(arr, (int)kMarkerAnchorCandidateIndex);
        if (anchor) {
            auto gpAnchor = g_guiMethods.transformGetGlobalPosition->invoke(anchor, ref::EmptyArgs());
            if (!gpAnchor.exception_thrown) {
                float ax = *reinterpret_cast<float*>(&gpAnchor.bytes[0]);
                float ay = *reinterpret_cast<float*>(&gpAnchor.bytes[4]);
                float az = *reinterpret_cast<float*>(&gpAnchor.bytes[8]);
                if (std::isfinite(ax) && std::isfinite(ay) && std::isfinite(az)
                    && fabsf(ax) <= 2400.f && fabsf(ay) <= 1600.f) {
                    markerX = ax;
                    markerY = ay;
                    markerZ = az;
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
            markerZ = *reinterpret_cast<float*>(&gp.bytes[8]);
            hasMarkerAnchor = std::isfinite(markerX) && std::isfinite(markerY) && std::isfinite(markerZ);
        }
    }

    float cleanX = (-markerX / fx) * kMarkerAssumedDepthMeters + g_posCleanX;
    float cleanY = ( markerY / fy) * kMarkerAssumedDepthMeters + g_posCleanY;
    float cleanZ = kMarkerAssumedDepthMeters + g_posCleanZ;
    if (cleanZ < 0.25f) cleanZ = 0.25f;

    float projectedX = 0.f, projectedY = 0.f;
    bool projected = ProjectCleanMarkerRayToHeadGui(cleanX, cleanY, cleanZ, fx, fy, projectedX, projectedY);

    float deltaX = -g_crosshair.tanRight * fx;
    float deltaY =  g_crosshair.tanUp * fy;
    if (projected) {
        deltaX = projectedX - markerX;
        deltaY = projectedY - markerY;
    }

    // Smooth marker delta to eliminate jitter from C-matrix noise,
    // position delta noise, FOV fluctuations, and anchor readback variance.
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
            "Marker comp: roll=%.1f anchor=(%.1f,%.1f) proj=(%.1f,%.1f,%d) delta=(%.1f,%.1f) pos=(%.3f,%.3f) C01=%.4f C10=%.4f",
            g_crosshair.rollDegrees,
            markerX, markerY,
            projectedX, projectedY, projected ? 1 : 0,
            deltaX, deltaY,
            g_posCleanX, g_posCleanY,
            g_C[0][1], g_C[1][0]);
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
