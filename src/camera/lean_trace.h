#pragma once

#include <cameraunlock/camera/lean_clamp.h>

namespace RE9HT::lean_trace {

// LeanQueryFn for core's LeanClamp: a line cast with the player's collision
// filter, from the un-leaned eye along the lean, in metres.
cameraunlock::camera::LeanObstruction Query(void* context, const cameraunlock::math::Vec3& start,
                                            const cameraunlock::math::Vec3& direction, float maxDistance);

} // namespace RE9HT::lean_trace
