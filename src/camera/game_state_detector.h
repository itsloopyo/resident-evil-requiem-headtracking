#pragma once

namespace RE9HT {

// Returns true if the player is in active gameplay (not paused, menu, loading, etc.)
bool IsInGameplay();

// Call periodically to refresh cached game state
void RefreshGameState();

} // namespace RE9HT
