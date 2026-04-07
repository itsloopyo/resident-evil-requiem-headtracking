#pragma once

namespace RE9HT {

inline constexpr const char* RE9HT_VERSION = "1.0.0";
inline constexpr const char* RE9HT_PLUGIN_NAME = "RE9 Head Tracking";

inline constexpr uint16_t DEFAULT_UDP_PORT = 4242;

inline constexpr int DEFAULT_TOGGLE_KEY = 0x23;           // VK_END
inline constexpr int DEFAULT_RECENTER_KEY = 0x24;          // VK_HOME
inline constexpr int DEFAULT_POSITION_TOGGLE_KEY = 0x21;   // VK_PRIOR (Page Up)
inline constexpr int DEFAULT_YAW_MODE_KEY = 0x22;          // VK_NEXT (Page Down)
inline constexpr int DEFAULT_DIAGNOSTIC_MARKER_KEY = 0x78; // VK_F9

} // namespace RE9HT
