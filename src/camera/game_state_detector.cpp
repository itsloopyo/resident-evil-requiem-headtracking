#include "pch.h"
#include "game_state_detector.h"

#include <cameraunlock/reframework/gameplay_gate.h>
#include <cameraunlock/reframework/manager_probe_checks.h>

namespace RE9HT {

namespace ref = cameraunlock::reframework;

// Requiem's managers do not answer to the fully-qualified names RE2 and RE3
// use, so the gate probes for them by shape instead. Nothing here reports a
// menu draw: on this title the manager probes separate the menus on their own.
static ref::GameplayGate g_gate{&ref::DiscoverManagerProbes, &ref::ManagerProbeGameplayCheck};

ref::GameplayGate* GameplayGateInstance() { return &g_gate; }

bool IsInGameplay() { return g_gate.IsInGameplay(); }

} // namespace RE9HT
