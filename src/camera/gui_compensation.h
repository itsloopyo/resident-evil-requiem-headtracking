#pragma once

namespace RE9HT {

// Resolve the GUI methods needed for crosshair/marker compensation.
// Called once during initialization.
void InitGUICompensationMethods();

// Per-element GUI draw callback dispatcher.
// Returns true to keep drawing the element, false to hide.
bool OnPreGuiDrawElement(void* element, void* context);

// Re-arm the element dumper — called from Mod::ToggleMarkersHidden.
void ResetGuiElementDumper();

} // namespace RE9HT
