#include "pch.h"
#include "aim_trace.h"

#include <cameraunlock/reframework/log_callback.h>
#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/reframework/tdb_inspector.h>

#include <reframework/API.hpp>
#include <span>
#include <string>

namespace RE9HT {

namespace ref = cameraunlock::reframework;

// Every overload of a method, with full parameter type names. LogMethodOverloads
// prints short names only, and the distinction between via.vec3, via.vec4 and
// via.physics.CastRayQuery is exactly what decides whether a call is safe to
// make, so print the full ones.
static void LogOverloadsVerbose(const char* typeName, const char* methodName) {
    const auto& api = reframework::API::get();
    auto type = api->tdb()->find_type(typeName);
    if (!type) {
        ref::LogInfo("  [trace] type not found: %s", typeName);
        return;
    }
    int found = 0;
    for (auto m : type->get_methods()) {
        if (!m) continue;
        const char* name = m->get_name();
        if (!name || strcmp(name, methodName) != 0) continue;
        found++;

        auto params = m->get_params();
        std::string sig;
        for (size_t i = 0; i < params.size(); i++) {
            if (i > 0) sig += ", ";
            auto* pt = reinterpret_cast<reframework::API::TypeDefinition*>(params[i].t);
            sig += pt ? pt->get_full_name() : std::string("?");
            sig += " ";
            sig += params[i].name ? params[i].name : "?";
        }
        auto rt = m->get_return_type();
        ref::LogInfo("  [trace] %s.%s(%s) -> %s  [params=%u]",
            typeName, methodName, sig.c_str(),
            rt ? rt->get_full_name().c_str() : "?", m->get_num_params());
    }
    if (found == 0) {
        ref::LogInfo("  [trace] %s has no method named %s", typeName, methodName);
    }
}

// Field layout of a value struct. ContactPoint reports zero methods, so its
// contents are reachable only as fields, and the offsets are what a caller has
// to read the hit position out of the returned bytes.
static void LogTypeFields(const char* typeName) {
    const auto& api = reframework::API::get();
    auto type = api->tdb()->find_type(typeName);
    if (!type) {
        ref::LogInfo("  [trace] type not found: %s", typeName);
        return;
    }
    ref::LogInfo("  [trace] === %s fields (size=%u) ===", typeName, type->get_size());
    for (auto f : type->get_fields()) {
        if (!f) continue;
        auto ft = f->get_type();
        ref::LogInfo("  [trace]   +0x%02X %s %s%s",
            f->get_offset_from_fieldptr(),
            ft ? ft->get_full_name().c_str() : "?",
            f->get_name() ? f->get_name() : "?",
            f->is_static() ? " [static]" : "");
    }
}

static void LogTypeSurface(const char* typeName) {
    const auto& api = reframework::API::get();
    auto type = api->tdb()->find_type(typeName);
    if (!type) {
        ref::LogInfo("  [trace] type not found: %s", typeName);
        return;
    }
    ref::LogInfo("  [trace] === %s ===", typeName);
    ref::EnumerateMethods(typeName, {});
}


// --- Resolved cast surface ---

static struct {
    void* physicsSystem = nullptr;                       // via.physics.System, a native singleton
    reframework::API::Method* castRay = nullptr;         // castRay(CastRayQuery) -> CastRayResult
    reframework::API::Method* setRay = nullptr;          // setRay(via.vec3 from, via.vec3 to)
    reframework::API::Method* enableAllHits = nullptr;
    reframework::API::Method* enableNearSort = nullptr;
    reframework::API::Method* setFilterInfo = nullptr;
    reframework::API::Method* numContactPoints = nullptr;
    reframework::API::Method* getContactPoint = nullptr;
    reframework::API::Method* getContactCollidable = nullptr;
    reframework::API::TypeDefinition* queryType = nullptr;
    bool ready = false;
} g_cast;

// Started slightly down-range so the ray does not open inside the player's own
// collision and report a hit at zero. Added back to the reported distance.
constexpr float kTraceStartOffset = 0.15f;
constexpr float kTraceRange = 200.0f;
// Collision layers that carry geometry a bullet stops on. Every contact seen on
// a ray belongs to one of five layers, and only these three are solid:
//
//   layer 1  props (mask 0x80) and stage section volumes (mask 0)
//   layer 5  props (mask 0x1)
//   layer 8  "e_r_col", the environment ray collision, and props (mask 0x1)
//   layer 6  the player, characters, Interact and AreaHit trigger volumes
//   layer 15 AreaHit sound triggers
constexpr uint32_t kGeometryLayers[] = { 1, 5, 8 };

static bool ResolveCastSurface() {
    const auto& api = reframework::API::get();

    g_cast.physicsSystem = api->get_native_singleton("via.physics.System");
    g_cast.castRay = ref::FindMethodByParamCount("via.physics.System", "castRay", 1);
    g_cast.setRay = ref::FindMethodByParamCount("via.physics.CastRayQuery", "setRay", 2);
    // Every hit, nearest first. enableOneHitBreak takes the closest thing on
    // the ray unconditionally, which is the player's own weapon sitting about
    // 0.65m ahead - so the distance collapses to the gun exactly when the player
    // backs away from the wall, and the correction grows when it should shrink.
    g_cast.enableAllHits = ref::FindMethodByParamCount("via.physics.CastRayQuery", "enableAllHits", 0);
    g_cast.enableNearSort = ref::FindMethodByParamCount("via.physics.CastRayQuery", "enableNearSort", 0);
    g_cast.setFilterInfo = ref::FindMethodByParamCount("via.physics.CastRayQuery", "set_FilterInfo", 1);
    g_cast.numContactPoints = ref::FindMethodByParamCount("via.physics.CastRayResult", "get_NumContactPoints", 0);
    g_cast.getContactPoint = ref::FindMethodByParamCount("via.physics.CastRayResult", "getContactPoint", 1);
    g_cast.getContactCollidable = ref::FindMethodByParamCount("via.physics.CastRayResult", "getContactCollidable", 1);

    // The type, not an instance. A query created once and held across frames is
    // a raw pointer to a managed object with nothing rooting it, so a GC pass is
    // free to move or collect it out from under the next trace. Each cast
    // creates its own, which also keeps a filter set by one cast off the next.
    g_cast.queryType = api->tdb()->find_type("via.physics.CastRayQuery");

    ref::LogInfo(
        "  [trace] resolved: system=%p castRay=%p setRay=%p allHits=%p numPoints=%p getPoint=%p queryType=%p setFilterInfo=%p",
        g_cast.physicsSystem, (void*)g_cast.castRay, (void*)g_cast.setRay,
        (void*)g_cast.enableAllHits, (void*)g_cast.numContactPoints,
        (void*)g_cast.getContactPoint, (void*)g_cast.queryType, (void*)g_cast.setFilterInfo);

    g_cast.ready = g_cast.physicsSystem && g_cast.castRay && g_cast.setRay
                && g_cast.numContactPoints && g_cast.getContactPoint && g_cast.queryType;
    if (!g_cast.ready) {
        ref::LogError("Aim trace unavailable - the reticle falls back to projecting the aim as a "
            "direction, which is correct at long range and drifts under a lean at close range");
    }
    return g_cast.ready;
}

// The getters below run for every contact of every cast, so each Method* is
// resolved once from the first object seen and invoked directly, rather than
// through invoke("name"), which searches the type's methods by string per call.
static reframework::API::Method* g_collidableGetGameObject = nullptr;
static reframework::API::Method* g_collidableGetFilterInfo = nullptr;
static reframework::API::Method* g_gameObjectGetName = nullptr;
static reframework::API::Method* g_filterGetLayer = nullptr;
static reframework::API::Method* g_filterGetMaskBits = nullptr;

static reframework::API::ManagedObject* ContactCollidable(reframework::API::ManagedObject* result,
                                                          uint32_t index) {
    if (!g_cast.getContactCollidable) return nullptr;
    void* args[1] = { (void*)(uintptr_t)index };
    auto cr = g_cast.getContactCollidable->invoke(result, std::span<void*>(args, 1));
    if (cr.exception_thrown) return nullptr;
    return reinterpret_cast<reframework::API::ManagedObject*>(cr.ptr);
}

// GameObject name behind a contact, for the log.
static void ContactName(reframework::API::ManagedObject* result, uint32_t index,
                        char* out, size_t outSize) {
    snprintf(out, outSize, "?");
    auto col = ContactCollidable(result, index);
    if (!col) return;
    auto orr = ref::InvokeCached(col, g_collidableGetGameObject, "get_GameObject", ref::EmptyArgs());
    if (orr.exception_thrown || !orr.ptr) return;
    auto ow = reinterpret_cast<reframework::API::ManagedObject*>(orr.ptr);
    auto nr = ref::InvokeCached(ow, g_gameObjectGetName, "get_Name", ref::EmptyArgs());
    if (!nr.exception_thrown && nr.ptr) ref::ReadManagedString(nr.ptr, out, outSize);
}

// The collision layer and mask behind a contact.
static bool ContactFilter(reframework::API::ManagedObject* result, uint32_t index,
                          uint32_t& layer, uint32_t& mask) {
    auto col = ContactCollidable(result, index);
    if (!col) return false;
    auto fi = ref::InvokeCached(col, g_collidableGetFilterInfo, "get_FilterInfo", ref::EmptyArgs());
    if (fi.exception_thrown || !fi.ptr) return false;
    auto f = reinterpret_cast<reframework::API::ManagedObject*>(fi.ptr);
    auto lr = ref::InvokeCached(f, g_filterGetLayer, "get_Layer", ref::EmptyArgs());
    auto mr = ref::InvokeCached(f, g_filterGetMaskBits, "get_MaskBits", ref::EmptyArgs());
    if (lr.exception_thrown || mr.exception_thrown) return false;
    layer = lr.dword;
    mask = mr.dword;
    return true;
}

// How far down the aim a contact sits, from the contact's own world position.
// The Distance field at +0x24 agrees with this to the centimetre wherever it was
// checked, but it was read off a field dump and never confirmed to hold a
// distance along the ray, where a position projected onto the aim is one by
// construction.
static float ContactAxialDistance(const float* contactPos, const float origin[3],
                                  const float forward[3]) {
    return (contactPos[0] - origin[0]) * forward[0]
         + (contactPos[1] - origin[1]) * forward[1]
         + (contactPos[2] - origin[2]) * forward[2];
}

// Whether a bullet would stop on this contact.
//
// MaskBits is the set of things a collider interacts with, so a mask of zero
// interacts with nothing and cannot stop anything. That single test separates
// the stage section volumes from real geometry: "st30_015_COL", "st30_014_COL"
// and "st30_062_COL" are level streaming sections, all on layer 1 with mask 0,
// and one ray down a corridor crossed "st30_015_COL" eight times. Taking the
// nearest measured to the near face of a streaming box - 0.66m while the player
// aimed across a 13m room, oversizing the parallax twentyfold.
//
// "e_r_col" is the environment ray collision and is what a shot stops on. It
// used to be skipped on the theory that it rode along with an entity at a fixed
// distance, "pinned at 2.72m". On a ray crossing it eleven times between 2.07m
// and 16.18m, it plainly is not that. In one capture 163 contacts on it were
// discarded while 187 on streaming volumes were taken.
//
// An allow-list, not a list of things to skip: the skip list was a blacklist of
// GameObject name prefixes, and a blacklist can never be finished. It missed
// "AreaHit_AutoSave", then "AreaHit_SoundSetState", each one collapsing the
// measured distance onto a trigger the player happened to be standing in.
static bool IsBulletBlocking(uint32_t layer, uint32_t mask) {
    if (mask == 0) return false;
    for (uint32_t solid : kGeometryLayers) {
        if (layer == solid) return true;
    }
    return false;
}

bool CastFirstBlocking(const float from[3], const float to[3], BlockingHit& out,
                       reframework::API::ManagedObject* filter, bool nameContact) {
    if (!g_cast.ready) return false;

    auto query = g_cast.queryType->create_instance();
    if (!query) return false;

    // via.vec3 is 16-byte aligned in RE Engine, so pass 4 floats.
    alignas(16) float from4[4] = { from[0], from[1], from[2], 0.f };
    alignas(16) float to4[4] = { to[0], to[1], to[2], 0.f };

    void* rayArgs[2] = { (void*)&from4[0], (void*)&to4[0] };
    auto setRet = g_cast.setRay->invoke(query, std::span<void*>(rayArgs, 2));
    if (setRet.exception_thrown) return false;

    if (g_cast.enableAllHits) g_cast.enableAllHits->invoke(query, ref::EmptyArgs());
    if (g_cast.enableNearSort) g_cast.enableNearSort->invoke(query, ref::EmptyArgs());
    if (filter) {
        if (!g_cast.setFilterInfo) return false;
        auto filterRet = ref::InvokeMethodWithArg(g_cast.setFilterInfo, query, filter);
        if (filterRet.exception_thrown) return false;
    }

    void* castArgs[1] = { (void*)query };
    auto castRet = g_cast.castRay->invoke(
        reinterpret_cast<reframework::API::ManagedObject*>(g_cast.physicsSystem),
        std::span<void*>(castArgs, 1));
    if (castRet.exception_thrown || !castRet.ptr) return false;

    auto result = reinterpret_cast<reframework::API::ManagedObject*>(castRet.ptr);
    auto countRet = g_cast.numContactPoints->invoke(result, ref::EmptyArgs());
    if (countRet.exception_thrown) return false;

    out.blocked = false;
    out.contacts = countRet.dword;
    snprintf(out.name, sizeof(out.name), "?");

    // Nearest-first (enableNearSort), so the first blocking contact is the one
    // the ray reaches.
    const uint32_t contacts = countRet.dword < 32 ? countRet.dword : 32;
    for (uint32_t i = 0; i < contacts; i++) {
        // Filter before reading the point: most contacts are rejected here.
        if (!filter) {
            uint32_t layer = 0, mask = 0;
            if (!ContactFilter(result, i, layer, mask)) continue;
            if (!IsBulletBlocking(layer, mask)) continue;
        }
        void* pointArgs[1] = { (void*)(uintptr_t)i };
        auto pr = g_cast.getContactPoint->invoke(result, std::span<void*>(pointArgs, 1));
        if (pr.exception_thrown) continue;
        if (nameContact) ContactName(result, i, out.name, sizeof(out.name));
        // via.physics.ContactPoint: Position at +0x00, Normal at +0x10.
        const float* point = reinterpret_cast<const float*>(&pr.bytes[0]);
        for (int k = 0; k < 3; k++) {
            out.position[k] = point[k];
            out.normal[k] = point[4 + k];
        }
        out.blocked = true;
        break;
    }
    return true;
}

bool TryGetAimDistance(const float origin[3], const float forward[3], float& outMetres) {
    const float from[3] = {
        origin[0] + forward[0] * kTraceStartOffset,
        origin[1] + forward[1] * kTraceStartOffset,
        origin[2] + forward[2] * kTraceStartOffset };
    const float to[3] = {
        origin[0] + forward[0] * kTraceRange,
        origin[1] + forward[1] * kTraceRange,
        origin[2] + forward[2] * kTraceRange };

    // No smoothing. The reticle is glued to a surface, so when the aim crosses
    // an edge the impact point genuinely jumps and the reticle is supposed to
    // jump with it. The 0.25-per-update lerp this replaced ran at 15Hz, a
    // quarter-second time constant, and spent that quarter second placing the
    // reticle for a depth the player was no longer aiming at.
    //
    // What the ray stopped on is named on a sample of casts. A distance of a
    // metre or two is either the wall being aimed at or the player's own body,
    // and the parallax is lean/distance, so mistaking one for the other scales
    // the whole correction wrong.
    static int s_frame = 0;
    static int s_left = 20;
    const bool logThisCast = s_left > 0 && (s_frame++ % 20) == 0;

    BlockingHit contact;
    if (!CastFirstBlocking(from, to, contact, nullptr, logThisCast) || !contact.blocked) return false;
    const float hit = ContactAxialDistance(contact.position, origin, forward);
    if (!(hit > 0.1f) || !(hit < kTraceRange * 1.5f)) return false;

    if (logThisCast) {
        s_left--;
        ref::LogInfo("Aim trace: hit=%.2fm points=%u on \"%s\"",
            hit, contact.contacts, contact.name);
    }

    outMetres = hit;
    return true;
}

void InitAimTrace() {
    const auto& api = reframework::API::get();

    ref::LogInfo("=== AIM TRACE DISCOVERY ===");

    // The ray-cast entry point. In RE Engine this is a native singleton, so it
    // is fetched by name rather than constructed.
    void* physicsSystem = api->get_native_singleton("via.physics.System");
    ref::LogInfo("  [trace] via.physics.System native singleton = %p", physicsSystem);

    auto sysType = api->tdb()->find_type("via.physics.System");
    ref::LogInfo("  [trace] via.physics.System type = %p", (void*)sysType);

    // Anything cast-shaped, whatever it happens to be called in this title.
    ref::EnumerateMethods("via.physics.System", { "ast", "ay", "verlap", "ntersect" });

    LogOverloadsVerbose("via.physics.System", "castRay");
    LogOverloadsVerbose("via.physics.System", "castRayAsync");
    LogOverloadsVerbose("via.physics.System", "sphereCast");

    // The query and result objects the cast takes. Their setters are what a
    // trace has to fill in, and their getters are where the distance comes out.
    LogTypeSurface("via.physics.CastRayQuery");
    LogTypeSurface("via.physics.CastRayResult");
    // How the ray goes in and how the hit comes out. setRay decides whether the
    // ray is handed over as two points or as a via.Ray, and the contact point is
    // where the distance the parallax needs actually lives.
    LogOverloadsVerbose("via.physics.CastRayQuery", "setRay");
    LogOverloadsVerbose("via.physics.CastRayResult", "getContactPoint");
    LogTypeSurface("via.physics.ContactPoint");
    LogTypeFields("via.physics.ContactPoint");
    LogTypeFields("via.physics.CastRayQuery");
    LogTypeSurface("via.Ray");

    // A filter is usually mandatory - an unfiltered cast hits the player's own
    // collision first and reports a distance of nearly zero.
    LogTypeSurface("via.physics.FilterInfo");

    ResolveCastSurface();

    ref::LogInfo("=== END AIM TRACE DISCOVERY ===");
}

} // namespace RE9HT
