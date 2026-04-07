#include "pch.h"
#include "gui_diagnostics.h"
#include "camera_internal.h"
#include "core/mod.h"
#include "core/logger.h"
#include "game_state_detector.h"

#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/reframework/tdb_inspector.h>

#include <reframework/API.hpp>
#include <unordered_set>
#include <string>

namespace RE9HT {

namespace ref = cameraunlock::reframework;

// Only GUI elements whose GameObject name starts with one of these prefixes
// get dumped.
static const char* const g_dumpGoNameAllowPrefixes[] = {
    "Gui_ui2010",  // world-anchored interaction prompt
    "Gui_ui2020",  // crosshair/reticle
    "Gui_ui2021",  // secondary crosshair element
    "Gui_ui2050",  // ammo display
};

static std::unordered_set<std::string> g_dumpedGuiKeys;
static int g_dumpCount = 0;
static constexpr int MAX_GUI_DUMPS = 64;

void ResetGuiDiagnostics() {
    g_dumpedGuiKeys.clear();
    g_dumpCount = 0;
    Logger::Instance().Info("GUI element dumper re-armed (will dump next unique sightings)");
}

void DiscoverGUICameraAccess() {
    Logger::Instance().Info("=== GUICamera discovery (iteration 3) ===");
    ref::LogMethodOverloads("via.Scene", "findComponents");
    ref::LogMethodOverloads("via.SceneManager", "get_CurrentScene");
    ref::LogMethodOverloads("via.SceneManager", "get_MainScene");
    ref::LogMethodOverloads("via.gui.GUICamera", "set_ScreenOffset");
    ref::LogMethodOverloads("via.gui.GUICamera", "get_ScreenOffset");
    ref::LogMethodOverloads("via.gui.GUI", "findObjects");
    ref::LogMethodOverloads("via.gui.GUI", "getObject");
    ref::EnumerateMethods("via.gui.PlayObject", {});
    ref::EnumerateMethods("via.gui.TransformObject", {});
    ref::EnumerateMethods("via.gui.Control", {});
    ref::EnumerateMethods("via.Camera", {});
    Logger::Instance().Info("=== end discovery ===");
}

void ScanGuiGoName(const char* goName, const char* tns, const char* tnm) {
    static std::unordered_set<std::string> s_seenGoNames;
    static bool s_scanDone = false;
    if (s_scanDone) return;

    std::string nameStr(goName);
    if (s_seenGoNames.insert(nameStr).second) {
        Logger::Instance().Info("GUI scan: GO=\"%s\" type=%s.%s",
            goName, tns ? tns : "", tnm ? tnm : "?");
    }
    if (s_seenGoNames.size() >= 100) {
        Logger::Instance().Info("GUI scan complete: %zu unique GO names logged", s_seenGoNames.size());
        s_scanDone = true;
    }
}

void TryDumpContext(void* context) {
    static bool s_ctxDumped = false;
    static int s_ctxDelay = 0;
    if (s_ctxDumped || !context || !Mod::Instance().IsEnabled()) return;
    if (++s_ctxDelay <= 120) return;

    s_ctxDumped = true;
    Logger::Instance().Info("=== CONTEXT DUMP (on_pre_gui_draw_element) ===");
    Logger::Instance().Info("  context ptr = %p", context);
    uint8_t* bytes = reinterpret_cast<uint8_t*>(context);
    for (int line = 0; line < 4; line++) {
        int off = line * 32;
        char hex[128] = {};
        for (int i = 0; i < 32; i++)
            sprintf(hex + i * 3, "%02X ", bytes[off + i]);
        Logger::Instance().Info("  +%02X: %s", off, hex);
    }
    float* floats = reinterpret_cast<float*>(context);
    Logger::Instance().Info("  as float[0..7]:  %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f",
        floats[0], floats[1], floats[2], floats[3], floats[4], floats[5], floats[6], floats[7]);
    Logger::Instance().Info("  as float[8..15]: %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f",
        floats[8], floats[9], floats[10], floats[11], floats[12], floats[13], floats[14], floats[15]);
    Logger::Instance().Info("  as float[16..23]: %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f",
        floats[16], floats[17], floats[18], floats[19], floats[20], floats[21], floats[22], floats[23]);
    Logger::Instance().Info("  as float[24..31]: %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f",
        floats[24], floats[25], floats[26], floats[27], floats[28], floats[29], floats[30], floats[31]);
    uint64_t* ptrs = reinterpret_cast<uint64_t*>(context);
    Logger::Instance().Info("  as ptr[0..7]: %p %p %p %p %p %p %p %p",
        (void*)ptrs[0], (void*)ptrs[1], (void*)ptrs[2], (void*)ptrs[3],
        (void*)ptrs[4], (void*)ptrs[5], (void*)ptrs[6], (void*)ptrs[7]);
    Logger::Instance().Info("=== END CONTEXT DUMP ===");
}

void TryDumpMatrixDiagnostic() {
    static bool s_matDiagDone = false;
    static int s_matDiagDelay = 0;
    if (s_matDiagDone || !g_cleanCameraMatrix.valid || !Mod::Instance().IsEnabled()) return;
    if (++s_matDiagDelay <= 60) return;

    void* tx = ResolveCameraTransformInternal();
    if (!tx) return;

    constexpr int TX_WORLDMATRIX_OFFSET = 0x80;
    Matrix4x4f* live = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(tx) + TX_WORLDMATRIX_OFFSET);
    const Matrix4x4f& clean = g_cleanCameraMatrix.matrix;
    float diffFwd = fabsf(live->m[2][0] - clean.m[2][0])
                  + fabsf(live->m[2][1] - clean.m[2][1])
                  + fabsf(live->m[2][2] - clean.m[2][2]);
    Logger::Instance().Info("GUI matrix diag: live fwd=(%.4f,%.4f,%.4f) clean fwd=(%.4f,%.4f,%.4f) diff=%.6f => %s",
        live->m[2][0], live->m[2][1], live->m[2][2],
        clean.m[2][0], clean.m[2][1], clean.m[2][2],
        diffFwd, diffFwd < 0.001f ? "CLEAN (body-locked)" : "HEAD-TRACKED (screen-locked)");
    s_matDiagDone = true;
}

// Walk the child PlayObject tree of a GUI via findObjects(typeof(PlayObject)).
void DumpChildTree(reframework::API::ManagedObject* guiMo, int indent) {
    if (!guiMo) {
        Logger::Instance().Info("%*s[child walk skipped: null GUI]", indent, "");
        return;
    }

    const auto& api = reframework::API::get();
    auto playObjType = api->typeof("via.gui.PlayObject");
    if (!playObjType) {
        Logger::Instance().Info("%*s[child walk skipped: PlayObject type not found]", indent, "");
        return;
    }

    // Find the findObjects(Type) method
    auto guiType = api->tdb()->find_type("via.gui.GUI");
    if (!guiType) return;
    reframework::API::Method* findObjectsByType = nullptr;
    for (auto m : guiType->get_methods()) {
        if (!m) continue;
        const char* name = m->get_name();
        if (!name || strcmp(name, "findObjects") != 0) continue;
        if (m->get_num_params() != 1) continue;
        auto params = m->get_params();
        if (params.size() == 1 && params[0].t) {
            auto pt = reinterpret_cast<reframework::API::TypeDefinition*>(params[0].t);
            if (pt && pt->get_name() && strcmp(pt->get_name(), "Type") == 0) {
                findObjectsByType = m;
                break;
            }
        }
    }
    if (!findObjectsByType) {
        Logger::Instance().Info("%*s[child walk skipped: findObjects(Type) not found]", indent, "");
        return;
    }

    std::vector<void*> args = { (void*)playObjType };
    auto ret = findObjectsByType->invoke(guiMo, args);
    if (ret.exception_thrown) {
        Logger::Instance().Info("%*s[findObjects(PlayObject) threw]", indent, "");
        return;
    }
    if (!ret.ptr) {
        Logger::Instance().Info("%*s[findObjects(PlayObject) returned null]", indent, "");
        return;
    }

    auto arr = reinterpret_cast<reframework::API::ManagedObject*>(ret.ptr);
    auto lenRet = arr->invoke("get_Length", ref::EmptyArgs());
    uint32_t len = lenRet.exception_thrown ? 0 : lenRet.dword;
    Logger::Instance().Info("%*s[child PlayObjects: %u]", indent, "", len);
    if (len == 0) return;

    uint32_t cap = len < 64 ? len : 64;

    for (uint32_t i = 0; i < cap; i++) {
        auto child = ref::ArrayGetValue(arr, (int)i);
        if (!child) continue;

        auto td = child->get_type_definition();
        const char* cns = (td && td->get_namespace()) ? td->get_namespace() : "";
        const char* cnm = (td && td->get_name()) ? td->get_name() : "?";
        Logger::Instance().Info("%*s  child[%u]: type=%s.%s ptr=%p", indent, "", i, cns, cnm, child);

        // PlayObject-level getters
        ref::LogGetterString(child, "get_Name",       "    Name");
        ref::LogGetterString(child, "get_Tag",        "    Tag");
        ref::LogGetterBool(child,   "get_Visible",    "    Visible");
        ref::LogGetterBool(child,   "get_ActualVisible", "    ActualVisible");
        ref::LogGetterU32(child,    "get_Priority",   "    Priority");

        // Control-level getters
        ref::LogGetterBool(child,   "get_Interactive",   "    Interactive");
        ref::LogGetterBool(child,   "get_UseInput",      "    UseInput");
        ref::LogGetterBool(child,   "get_Play",          "    Play");
        ref::LogGetterBool(child,   "get_LoopState",     "    LoopState");
        ref::LogGetterU32(child,    "get_StatePattern",  "    StatePattern");

        auto tryFloat = [&](const char* m, const char* label) {
            auto r = child->invoke(m, ref::EmptyArgs());
            if (r.exception_thrown) return;
            Logger::Instance().Info("%*s    %s = %.4f", indent, "", label, r.f);
        };
        auto tryVec2 = [&](const char* m, const char* label) {
            auto r = child->invoke(m, ref::EmptyArgs());
            if (r.exception_thrown) return;
            float x = *reinterpret_cast<float*>(&r.bytes[0]);
            float y = *reinterpret_cast<float*>(&r.bytes[4]);
            Logger::Instance().Info("%*s    %s = (%.3f, %.3f)", indent, "", label, x, y);
        };
        auto tryVec3 = [&](const char* m, const char* label) {
            auto r = child->invoke(m, ref::EmptyArgs());
            if (r.exception_thrown) return;
            float x = *reinterpret_cast<float*>(&r.bytes[0]);
            float y = *reinterpret_cast<float*>(&r.bytes[4]);
            float z = *reinterpret_cast<float*>(&r.bytes[8]);
            Logger::Instance().Info("%*s    %s = (%.3f, %.3f, %.3f)", indent, "", label, x, y, z);
        };
        auto tryVec4 = [&](const char* m, const char* label) {
            auto r = child->invoke(m, ref::EmptyArgs());
            if (r.exception_thrown) return;
            float x = *reinterpret_cast<float*>(&r.bytes[0]);
            float y = *reinterpret_cast<float*>(&r.bytes[4]);
            float z = *reinterpret_cast<float*>(&r.bytes[8]);
            float w = *reinterpret_cast<float*>(&r.bytes[12]);
            Logger::Instance().Info("%*s    %s = (%.3f, %.3f, %.3f, %.3f)", indent, "", label, x, y, z, w);
        };

        tryFloat("get_PlayFrame",        "PlayFrame");
        tryFloat("get_PlaySpeed",        "PlaySpeed");
        tryFloat("get_StateFinishFrame", "StateFinishFrame");
        tryVec4("get_ColorScale",   "ColorScale");
        tryVec3("get_ColorOffset",  "ColorOffset");
        tryFloat("get_Saturation",  "Saturation");
        tryVec3("get_Position",   "Position");
        tryVec3("get_Pos",        "Pos");
        tryVec2("get_Position2D", "Position2D");
        tryVec2("get_Offset",     "Offset");
        tryVec2("get_Size",       "Size");
        tryVec2("get_Pivot",      "Pivot");
        tryVec3("get_Scale",      "Scale");
        tryVec3("get_Rotation",   "Rotation");
        tryVec2("get_Anchor",     "Anchor");
        ref::LogGetterString(child, "get_Text",    "    Text");
        ref::LogGetterString(child, "get_Message", "    Message");
        ref::LogGetterPtr(child, "get_Texture",   "    Texture");
        ref::LogGetterPtr(child, "get_Sprite",    "    Sprite");
        ref::LogGetterPtr(child, "get_Image",     "    Image");
        ref::LogGetterPtr(child, "get_Material",  "    Material");
        ref::LogGetterPtr(child, "get_Font",      "    Font");
        tryVec2("get_UV",     "UV");
        tryVec2("get_UVSize", "UVSize");

        ref::DumpFieldsForType(td, child, indent + 4);
    }
}

bool TryDumpGuiElement(
    reframework::API::ManagedObject* mo,
    reframework::API::TypeDefinition* td,
    const char* goName,
    reframework::API::ManagedObject* goMo)
{
    if (!td) return false;
    const char* tns = td->get_namespace();
    const char* tnm = td->get_name();
    if (!tnm) return false;

    // Check name prefix filter
    bool nameAllowed = false;
    for (const char* prefix : g_dumpGoNameAllowPrefixes) {
        size_t plen = strlen(prefix);
        if (strncmp(goName, prefix, plen) == 0) { nameAllowed = true; break; }
    }
    if (!nameAllowed) return false;

    // Build dedupe key
    std::string key;
    key.reserve(strlen(goName) + (tnm ? strlen(tnm) : 0) + 2);
    key += goName;
    key += '|';
    if (tns && *tns) { key += tns; key += '.'; }
    key += tnm;

    if (g_dumpCount >= MAX_GUI_DUMPS) return false;
    if (!g_dumpedGuiKeys.insert(key).second) return false;

    g_dumpCount++;
    Logger::Instance().Info("=== GUI element dump #%d: %s ===", g_dumpCount, key.c_str());

    // Type inheritance chain
    {
        auto cur = td;
        int d = 0;
        while (cur && d < 6) {
            Logger::Instance().Info("  type[%d]: %s.%s", d,
                cur->get_namespace() ? cur->get_namespace() : "",
                cur->get_name() ? cur->get_name() : "?");
            cur = cur->get_parent_type();
            d++;
        }
    }

    // Well-known GUI getters
    ref::LogGetterString(mo, "get_AssetPath", "AssetPath");
    ref::LogGetterPtr(mo,    "get_Asset",     "Asset");
    ref::LogGetterPtr(mo,    "get_View",      "View");
    ref::LogGetterPtr(mo,    "get_SceneView", "SceneView");
    ref::LogGetterU32(mo,    "get_GUICameraTargetID", "GUICameraTargetID");
    ref::LogGetterU32(mo,    "get_RenderOutputID",    "RenderOutputID");
    ref::LogGetterU32(mo,    "get_Segment",           "Segment");
    ref::LogGetterBool(mo,   "get_Ready",             "Ready");
    ref::LogGetterString(mo, "get_Name",       "Name");
    ref::LogGetterString(mo, "get_Tag",        "Tag");
    ref::LogGetterBool(mo,   "get_Visible",    "Visible");
    ref::LogGetterBool(mo,   "get_ActualVisible", "ActualVisible");
    ref::LogGetterU32(mo,    "get_Priority",   "Priority");

    Logger::Instance().Info("  GameObject.Name = \"%s\" (ptr=%p)", goName, (void*)goMo);

    // Walk the GO's Transform parent chain
    if (goMo) {
        auto txRet = goMo->invoke("get_Transform", ref::EmptyArgs());
        if (!txRet.exception_thrown && txRet.ptr) {
            auto txMo = reinterpret_cast<reframework::API::ManagedObject*>(txRet.ptr);
            for (int d = 0; d < 6; d++) {
                auto pGoRet = txMo->invoke("get_GameObject", ref::EmptyArgs());
                if (pGoRet.exception_thrown || !pGoRet.ptr) break;
                auto pGo = reinterpret_cast<reframework::API::ManagedObject*>(pGoRet.ptr);
                char pName[128] = "?";
                auto pNameRet = pGo->invoke("get_Name", ref::EmptyArgs());
                if (!pNameRet.exception_thrown && pNameRet.ptr) {
                    ref::ReadManagedString(pNameRet.ptr, pName, sizeof(pName));
                }
                Logger::Instance().Info("  transform.parent[%d].GO = \"%s\"", d, pName);
                auto parentRet = txMo->invoke("get_Parent", ref::EmptyArgs());
                if (parentRet.exception_thrown || !parentRet.ptr) break;
                txMo = reinterpret_cast<reframework::API::ManagedObject*>(parentRet.ptr);
            }
        }
    }

    ref::DumpFieldsRecursive(td, mo, 2);
    DumpChildTree(mo, 2);

    Logger::Instance().Info("=== end dump #%d ===", g_dumpCount);
    return true;
}

} // namespace RE9HT
