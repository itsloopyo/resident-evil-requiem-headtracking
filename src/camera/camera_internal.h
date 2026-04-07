#pragma once

// Internal shared state between camera_hook.cpp, gui_compensation.cpp,
// and gui_diagnostics.cpp. Not part of the public API.

#include "math_types.h"
#include "camera_hook.h"

namespace RE9HT {

// Clean camera matrix saved before head tracking is applied each frame.
struct CleanCameraMatrix {
    Matrix4x4f matrix;
    bool valid = false;
};

// Shared per-frame state (defined in camera_hook.cpp)
extern CrosshairProjection g_crosshair;
extern CleanCameraMatrix g_cleanCameraMatrix;
extern float g_C[3][3];
extern bool g_C_valid;
extern float g_posCleanX, g_posCleanY, g_posCleanZ;

// Resolve the primary camera's transform via SceneManager chain.
void* ResolveCameraTransformInternal();

} // namespace RE9HT
