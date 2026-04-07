#pragma once

#include <cameraunlock/reframework/re_math.h>

namespace RE9HT {

using cameraunlock::reframework::kDegToRad;
using cameraunlock::reframework::Matrix4x4f;
using cameraunlock::reframework::REQuat;
using cameraunlock::reframework::MatrixToQuat;
using cameraunlock::reframework::QuatToMatrix3x3;
using cameraunlock::reframework::QuatMul;
using cameraunlock::reframework::QuatNorm;
using cameraunlock::reframework::ComputeCleanToHeadRotation;

// Legacy alias
constexpr float DEG_TO_RAD = kDegToRad;

} // namespace RE9HT
