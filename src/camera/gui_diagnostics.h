#pragma once

#include <reframework/API.hpp>

namespace RE9HT {

// Run one-shot GUI discovery (method enumeration for debugging).
// Called once during initialization.
void DiscoverGUICameraAccess();

// Attempt to dump a GUI element if it's a new unique sighting and passes
// the name prefix filter. Returns true if a dump was emitted.
bool TryDumpGuiElement(
    reframework::API::ManagedObject* mo,
    reframework::API::TypeDefinition* td,
    const char* goName,
    reframework::API::ManagedObject* goMo);

// Dump the on_pre_gui_draw_element context parameter (one-shot).
void TryDumpContext(void* context);

// Dump the camera matrix state during GUI rendering (one-shot).
void TryDumpMatrixDiagnostic();

// Log unique GUI GO names seen during gameplay (one-shot scan).
void ScanGuiGoName(const char* goName, const char* tns, const char* tnm);

// Reset the element dumper — re-arms so next sightings are captured.
void ResetGuiDiagnostics();

// Walk the child PlayObject tree of a GUI and dump each one.
void DumpChildTree(reframework::API::ManagedObject* guiMo, int indent);

} // namespace RE9HT
