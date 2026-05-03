#pragma once

namespace RE9HT {

// Center the game's top-level window on its current monitor's work area.
// Runs at most once per process; subsequent calls are no-ops so users can
// drag the window afterwards without us yanking it back. Skips windows
// already at/above the work-area size (true fullscreen, or borderless
// fullscreen the user is happy with).
//
// Reason for existing: on super-ultrawide setups (e.g. 5120x1440)
// RE:Requiem launches its window in the top-left corner and the title
// bar refuses drag input until the first cinematic ends, so the head
// tracking experience starts off-axis with no easy way to fix it.
void CenterGameWindowOnce();

} // namespace RE9HT
