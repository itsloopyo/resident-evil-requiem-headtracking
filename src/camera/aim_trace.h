#pragma once

#include <cstdint>

#include <reframework/API.hpp>

namespace RE9HT {

void InitAimTrace();

// Distance along the clean aim to the first bullet-blocking surface.
bool TryGetAimDistance(const float origin[3], const float forward[3], float& outMetres);

// The first contact between two points that blocks.
struct BlockingHit {
    bool blocked = false;
    float position[3] = {};
    float normal[3] = {};
    uint32_t contacts = 0;
    char name[128] = {};
};

// Casts from `from` to `to`. With no filter, a contact blocks when it is on a
// layer a bullet stops on; with a via.physics.FilterInfo, the engine applies it
// to the cast and the nearest contact blocks. False when the cast could not run
// at all; true with out.blocked false when it ran and nothing blocking lies
// between the two points.
bool CastFirstBlocking(const float from[3], const float to[3], BlockingHit& out,
                       reframework::API::ManagedObject* filter);

} // namespace RE9HT
