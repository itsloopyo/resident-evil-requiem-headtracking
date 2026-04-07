#include "pch.h"

#include <reframework/API.hpp>

#include "core/mod.h"
#include "core/logger.h"
#include "camera/camera_hook.h"
#include "camera/gui_compensation.h"

#include <cameraunlock/input/hotkey_poller.h>
#include <cameraunlock/reframework/log_callback.h>

static cameraunlock::input::HotkeyPoller g_hotkeyPoller;

static void OnPreBeginRendering() {
    RE9HT::OnPreBeginRendering();
}

static void OnPostBeginRendering() {
    RE9HT::OnPostBeginRendering();
}

static bool OnPreGuiDrawElement(void* element, void* context) {
    return RE9HT::OnPreGuiDrawElement(element, context);
}

// --- REFramework plugin exports ---

extern "C" __declspec(dllexport)
void reframework_plugin_required_version(REFrameworkPluginVersion* version) {
    version->major = REFRAMEWORK_PLUGIN_VERSION_MAJOR;
    version->minor = REFRAMEWORK_PLUGIN_VERSION_MINOR;
    version->patch = REFRAMEWORK_PLUGIN_VERSION_PATCH;
    version->game_name = nullptr;
}

extern "C" __declspec(dllexport)
bool reframework_plugin_initialize(const REFrameworkPluginInitializeParam* param) {
    if (!param) return false;

    // Initialize REFramework SDK wrapper
    reframework::API::initialize(param);

    // Set up logging via REFramework's log functions
    RE9HT::Logger::Instance().SetREFunctions(
        param->functions->log_info,
        param->functions->log_warn,
        param->functions->log_error
    );

    // Bridge shared library logging to REFramework's log functions
    cameraunlock::reframework::SetLogCallback([](cameraunlock::reframework::LogLevel level, const char* msg) {
        switch (level) {
            case cameraunlock::reframework::LogLevel::Warning:
                RE9HT::Logger::Instance().Warning("%s", msg); break;
            case cameraunlock::reframework::LogLevel::Error:
                RE9HT::Logger::Instance().Error("%s", msg); break;
            default:
                RE9HT::Logger::Instance().Info("%s", msg); break;
        }
    });

    RE9HT::Logger::Instance().Info("RE9 Head Tracking v%s - Plugin loaded", RE9HT::RE9HT_VERSION);

    // Initialize mod (tracking pipeline, UDP receiver)
    if (!RE9HT::Mod::Instance().Initialize()) {
        RE9HT::Logger::Instance().Error("Mod initialization failed");
        return false;
    }

    param->functions->on_pre_application_entry("BeginRendering", OnPreBeginRendering);
    param->functions->on_post_application_entry("BeginRendering", OnPostBeginRendering);
    // on_pre_gui_draw_element gives us per-element access for F9-hide + full info
    // dump. Cursor-based tracking suppression is disabled in game_state_detector,
    // so the earlier cursor-flicker interaction no longer affects tracking.
    param->functions->on_pre_gui_draw_element(OnPreGuiDrawElement);

    // Set up hotkeys
    auto& config = RE9HT::Mod::Instance().GetConfig();

    g_hotkeyPoller.SetToggleKey(config.toggleKey, []() {
        RE9HT::Mod::Instance().Toggle();
    });
    g_hotkeyPoller.SetRecenterKey(config.recenterKey, []() {
        RE9HT::Mod::Instance().Recenter();
    });
    g_hotkeyPoller.AddHotkey(config.positionToggleKey, []() {
        RE9HT::Mod::Instance().TogglePosition();
    });
    g_hotkeyPoller.AddHotkey(config.yawModeKey, []() {
        RE9HT::Mod::Instance().ToggleYawMode();
    });
    // F9 (diagnosticMarkerKey slot): toggle hiding of world-anchored GUI markers.
    // The on_pre_gui_draw_element callback checks AreMarkersHidden() and returns
    // false for marker elements when the flag is set. Full marker info is dumped
    // to the log on first sight regardless of the flag.
    g_hotkeyPoller.AddHotkey(config.diagnosticMarkerKey, []() {
        RE9HT::Mod::Instance().ToggleMarkersHidden();
    });

    g_hotkeyPoller.Start();

    RE9HT::Logger::Instance().Info("Plugin initialization complete");
    return true;
}
