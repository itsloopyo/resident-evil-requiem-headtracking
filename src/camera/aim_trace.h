#pragma once

namespace RE9HT {

void InitAimTrace();

// Distance along the clean aim to the first bullet-blocking surface.
bool TryGetAimDistance(const float origin[3], const float forward[3], float& outMetres);

} // namespace RE9HT
