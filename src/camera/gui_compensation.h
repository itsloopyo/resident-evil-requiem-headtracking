#pragma once

namespace RE9HT {

// Per-element GUI draw callback dispatcher.
// Returns true to keep drawing the element, false to hide.
bool OnPreGuiDrawElement(void* element, void* context);

} // namespace RE9HT
