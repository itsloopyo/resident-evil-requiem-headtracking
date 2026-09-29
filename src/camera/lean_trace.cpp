#include "pch.h"
#include "lean_trace.h"

#include "aim_trace.h"
#include "player_rig.h"

#include <cameraunlock/reframework/plugin_mod.h>

namespace RE9HT::lean_trace {

using cameraunlock::camera::LeanObstruction;
using cameraunlock::math::Vec3;

// A line has no width, so the margin LeanClamp keeps along the ray is only the
// margin off the wall when the lean meets it head on. Met at an angle, the ray
// runs further before it reaches the surface than the eye sits from it, so the
// reported distance is shortened by the margin over the cosine of the approach,
// which holds the eye the margin off the wall measured along its normal. The
// floor stops a lean skimming along a wall from being held off it by metres.
constexpr float kMinApproachCos = 0.25f;

LeanObstruction Query(void*, const Vec3& start, const Vec3& direction, float maxDistance) {
    const float margin = cameraunlock::reframework::PluginMod::Instance().GetConfig().collisionMargin;

    // Past the lean by as far as the steepest approach needs, or the eye travels
    // the whole lean, arrives against the wall, and is only pushed back once the
    // head has moved far enough for the ray itself to cross the surface.
    const float range = maxDistance + margin * (1.0f / kMinApproachCos - 1.0f);
    const float from[3] = {start.x, start.y, start.z};
    const float to[3] = {start.x + direction.x * range, start.y + direction.y * range,
                         start.z + direction.z * range};

    LeanObstruction result;
    // The player's own collision filter, so the eye stops where the body would.
    // The layers the aim trace stops on miss walls the player collides with: one
    // wall in the care center answers only on layer 1 with mask 0, the layer and
    // mask the aim trace skips, with the bullet collision half a metre behind it.
    reframework::API::ManagedObject* filter = PlayerCollisionFilter();
    if (!filter) return result;
    BlockingHit hit;
    if (!CastFirstBlocking(from, to, hit, filter, false)) return result;
    result.queried = true;
    if (!hit.blocked) return result;

    const Vec3 point(hit.position[0], hit.position[1], hit.position[2]);
    const float along = Vec3::Dot(point - start, direction);
    if (!(along < range)) return result;

    float approach = std::fabs(Vec3::Dot(Vec3(hit.normal[0], hit.normal[1], hit.normal[2]), direction));
    if (!(approach >= kMinApproachCos)) approach = kMinApproachCos;

    result.blocked = true;
    result.distance = along - margin / approach + margin;
    return result;
}

} // namespace RE9HT::lean_trace
