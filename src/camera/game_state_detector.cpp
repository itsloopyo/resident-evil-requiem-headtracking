#include "pch.h"
#include "game_state_detector.h"
#include "camera_hook.h"
#include "core/logger.h"

#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/reframework/game_state_probing.h>

#include <reframework/API.hpp>

namespace RE9HT {

namespace ref = cameraunlock::reframework;

// Resolved method + singleton name pairs for runtime checks
using ref::MethodCheck;

static struct {
    bool inGameplay = false;
    uint64_t lastCheckTime = 0;
    static constexpr uint64_t CHECK_INTERVAL_MS = 100;

    bool typesInitialized = false;
    reframework::API::Method* getMainView = nullptr;
    reframework::API::Method* getPrimaryCamera = nullptr;
    reframework::API::Method* getGlobalSpeed = nullptr;

    // Discovered checks — each is a method that returns a value indicating non-gameplay
    MethodCheck isPaused;
    MethodCheck isPlayingEvent;
    MethodCheck isOpen;
    MethodCheck isSystemFlow;
    MethodCheck isEventFlow;
    MethodCheck isPlaying;
    MethodCheck isTransition;
    MethodCheck isEventPlaying;
    MethodCheck isMoviePlaying;
    MethodCheck isInputBlocked;
    MethodCheck isCutscene;
    MethodCheck situationType;

    // GuiManager boolean fields to probe during diagnostics
    struct GuiFieldProbe {
        reframework::API::Method* method = nullptr;
        const char* name = nullptr;
    };
    static constexpr int MAX_GUI_PROBES = 16;
    GuiFieldProbe guiProbes[MAX_GUI_PROBES];
    int guiProbeCount = 0;
    const char* guiManagerSingleton = nullptr;

    // Int checks
    MethodCheck pauseBits;
    MethodCheck flowStatus;

    // Pointer checks
    MethodCheck playerContext;

    // Input level: GuiOpenCloseData chain
    MethodCheck guiOpenClose;
    MethodCheck inputLevel;
    const char* guiSingletonName = nullptr;

    // Camera identity tracking for cinematic detection
    reframework::API::Method* getGameObject = nullptr;
    reframework::API::Method* getTransform = nullptr;
    reframework::API::Method* getName = nullptr;
    void* gameplayCameraGO = nullptr;
    bool gameplayCameraLocked = false;

    // Cursor visibility debounce (diagnostic only — not suppressing)
    int cursorVisibleCount = 0;
    int cursorHiddenCount = 0;
    bool cursorSuppressing = false;
    static constexpr int CURSOR_SUPPRESS_THRESHOLD = 3;
    static constexpr int CURSOR_RESUME_THRESHOLD = 15;

    // Transition tracking
    bool wasInGameplay = false;
    bool pendingRecenter = false;
    int diagBurstRemaining = 0;
} g_state;

static void DiscoverTypes() {
    const auto api = reframework::API::get().get();
    auto tdb = api->tdb();

    Logger::Instance().Info("=== Begin RE9 type/method discovery ===");

    // Camera basics
    auto smType = tdb->find_type("via.SceneManager");
    if (smType) g_state.getMainView = smType->find_method("get_MainView");
    auto svType = tdb->find_type("via.SceneView");
    if (svType) g_state.getPrimaryCamera = svType->find_method("get_PrimaryCamera");

    // GlobalSpeed
    auto appType = tdb->find_type("via.Application");
    if (appType) g_state.getGlobalSpeed = appType->find_method("get_GlobalSpeed");
    Logger::Instance().Info("GlobalSpeed: %s", g_state.getGlobalSpeed ? "found" : "NOT found");

    // --- Probe every known manager pattern from RE2-RE8 ---

    { const char* methods[] = {"get_isPaused", "get_IsPaused", "get_Paused"};
      ref::ProbeManager(tdb, api, "PauseManager", methods, 3, g_state.isPaused, "PauseManager.isPaused"); }
    { const char* methods[] = {"get_lastPauseBits", "get_PauseBits", "get_currentPauseFlag", "get_PauseFlag"};
      ref::ProbeManager(tdb, api, "PauseManager", methods, 4, g_state.pauseBits, "PauseManager.pauseBits"); }
    { const char* methods[] = {"get_IsPlayingEvent", "get_isPlayingEvent"};
      if (!ref::ProbeManager(tdb, api, "GuiManager", methods, 2, g_state.isPlayingEvent, "GuiManager.IsPlayingEvent"))
          ref::ProbeManager(tdb, api, "GUIManager", methods, 2, g_state.isPlayingEvent, "GUIManager.IsPlayingEvent"); }
    { const char* methods[] = {"get_CurrentSituationType", "get_SituationType"};
      if (!ref::ProbeManager(tdb, api, "GuiManager", methods, 2, g_state.situationType, "GuiManager.SituationType"))
          ref::ProbeManager(tdb, api, "GUIManager", methods, 2, g_state.situationType, "GUIManager.SituationType"); }
    { const char* methods[] = {"get_isOpen", "get_IsOpen"};
      if (!ref::ProbeManager(tdb, api, "GuiManager", methods, 2, g_state.isOpen, "GuiManager.isOpen"))
          ref::ProbeManager(tdb, api, "GUIManager", methods, 2, g_state.isOpen, "GUIManager.isOpen"); }
    { const char* methods[] = {"get_isEnableSystemFlow", "get_IsEnableSystemFlow"};
      if (!ref::ProbeManager(tdb, api, "GuiManager", methods, 2, g_state.isSystemFlow, "GuiManager.isEnableSystemFlow"))
          ref::ProbeManager(tdb, api, "GUIManager", methods, 2, g_state.isSystemFlow, "GUIManager.isEnableSystemFlow"); }
    { const char* methods[] = {"get_isEnableEventFlow", "get_IsEnableEventFlow"};
      if (!ref::ProbeManager(tdb, api, "GuiManager", methods, 2, g_state.isEventFlow, "GuiManager.isEnableEventFlow"))
          ref::ProbeManager(tdb, api, "GUIManager", methods, 2, g_state.isEventFlow, "GUIManager.isEnableEventFlow"); }
    { const char* methods[] = {"get_isPlaying", "get_IsPlaying"};
      if (!ref::ProbeManager(tdb, api, "SequenceManager", methods, 2, g_state.isPlaying, "SequenceManager.isPlaying"))
          ref::ProbeManager(tdb, api, "CutsceneManager", methods, 2, g_state.isPlaying, "CutsceneManager.isPlaying"); }
    { const char* methods[] = {"get_isRunningTransition", "get_IsRunningTransition"};
      ref::ProbeManager(tdb, api, "SceneTransitionManager", methods, 2, g_state.isTransition, "SceneTransitionManager.isRunningTransition"); }
    { const char* methods[] = {"get_Status", "get_CurrentStatus", "get_status"};
      ref::ProbeManager(tdb, api, "GameFlowManager", methods, 3, g_state.flowStatus, "GameFlowManager.Status"); }
    { const char* methods[] = {"getPlayerContextRef", "get_PlayerContextRef", "get_playerContextRef"};
      ref::ProbeManager(tdb, api, "CharacterManager", methods, 3, g_state.playerContext, "CharacterManager.playerCtx"); }
    if (!g_state.playerContext.method) {
        const char* methods[] = {"getCurrentSurvivor", "get_CurrentSurvivor"};
        ref::ProbeManager(tdb, api, "survivor.SurvivorManager", methods, 2, g_state.playerContext, "SurvivorManager.currentSurvivor");
    }

    // GuiOpenCloseData chain
    {
        const char* guiNames[] = {"GuiManager", "GUIManager"};
        for (auto gn : guiNames) {
            auto guiType = ref::FindType(tdb, gn);
            if (!guiType) continue;
            auto openCloseMethod = guiType->find_method("get_GuiOpenCloseData");
            if (!openCloseMethod) openCloseMethod = guiType->find_method("get_guiOpenCloseData");
            if (!openCloseMethod) continue;

            auto sn = ref::FindSingleton(api, gn);
            if (!sn) continue;

            const char* ocdNames[] = { "gui.GuiOpenCloseData", "GuiOpenCloseData" };
            for (auto ocdn : ocdNames) {
                auto ocdType = ref::FindType(tdb, ocdn);
                if (!ocdType) continue;
                const char* ilMethods[] = {"get_CurrActiveInputevel", "get_CurrActiveInputLevel"};
                auto ilMethod = ref::FindMethod(ocdType, ilMethods, 2);
                if (!ilMethod) continue;

                static char guiSnBuf[256];
                strncpy(guiSnBuf, sn, 255);
                guiSnBuf[255] = '\0';
                g_state.guiSingletonName = guiSnBuf;
                g_state.guiOpenClose.method = openCloseMethod;
                g_state.inputLevel.method = ilMethod;
                Logger::Instance().Info("Probe OK: GuiOpenCloseData -> CurrActiveInputLevel (singleton: %s)", guiSnBuf);
                goto doneOpenClose;
            }
        }
        doneOpenClose:;
    }

    // EventManager / MovieManager
    { const char* methods[] = {"get_isPlaying", "get_IsPlaying", "get_isEventPlaying", "get_IsEventPlaying"};
      if (!ref::ProbeManager(tdb, api, "EventManager", methods, 4, g_state.isEventPlaying, "EventManager.isPlaying"))
          ref::ProbeManager(tdb, api, "event.EventManager", methods, 4, g_state.isEventPlaying, "event.EventManager.isPlaying"); }
    { const char* methods[] = {"get_isPlaying", "get_IsPlaying", "get_isActive", "get_IsActive"};
      if (!ref::ProbeManager(tdb, api, "MovieManager", methods, 4, g_state.isMoviePlaying, "MovieManager.isPlaying"))
          ref::ProbeManager(tdb, api, "CinematicManager", methods, 4, g_state.isMoviePlaying, "CinematicManager.isPlaying"); }

    // InputManager / PlayerManager
    { const char* types[] = {"InputManager", "InputSystem", "PlayerInputManager", "PlayerManager"};
      const char* methods[] = {
          "get_isInputBlocked", "get_IsInputBlocked", "get_isBlocked",
          "get_isLocked", "get_IsLocked", "get_isPlayerControllable",
          "get_IsPlayerControllable", "get_isEnableInput", "get_IsEnableInput",
          "get_isDisableInput", "get_IsDisableInput",
      };
      for (auto tn : types) {
          if (g_state.isInputBlocked.method) break;
          ref::ProbeManager(tdb, api, tn, methods, 11, g_state.isInputBlocked, "InputBlock");
      }
    }

    // Broader cutscene/event probes
    { const char* types[] = {
          "CutSceneManager", "CutsceneController", "EventSceneManager",
          "DemoManager", "StoryManager", "ScenarioManager", "PlayEventManager",
      };
      const char* methods[] = {
          "get_isPlaying", "get_IsPlaying", "get_isActive", "get_IsActive",
          "get_isRunning", "get_IsRunning",
      };
      for (auto tn : types) {
          if (g_state.isCutscene.method) break;
          ref::ProbeManager(tdb, api, tn, methods, 6, g_state.isCutscene, tn);
      }
    }

    // GuiManager boolean probes for diagnostics
    {
        auto guiType = ref::FindType(tdb, "GuiManager");
        if (guiType) {
            auto sn = ref::FindSingleton(api, "GuiManager");
            if (sn) {
                static char guiSnBuf2[256];
                strncpy(guiSnBuf2, sn, 255);
                guiSnBuf2[255] = '\0';
                g_state.guiManagerSingleton = guiSnBuf2;

                const char* boolGetters[] = {
                    "get_Initialized", "get_IsSystemReady",
                    "get_IsFirstTimeItemGetRunning",
                    "get_canPauseInDemo", "get_canDemoSkip",
                    "get_IsQuickSaveExists", "get_IsOpenWorldMap",
                    "get_IsPause", "get_IsPaused", "get_isPause", "get_isPaused",
                    "get_IsMenu", "get_isMenu", "get_IsMenuOpen", "get_isMenuOpen",
                    "get_IsInventory", "get_isInventory", "get_IsInventoryOpen",
                    "get_IsMapOpen", "get_isMapOpen",
                    "get_IsEventSkip", "get_IsSubtitle",
                    "get_IsLoading", "get_isLoading",
                    "get_IsGameOver", "get_isGameOver",
                    "get_IsOption", "get_isOption",
                };
                for (auto getter : boolGetters) {
                    if (g_state.guiProbeCount >= g_state.MAX_GUI_PROBES) break;
                    auto m = guiType->find_method(getter);
                    if (m) {
                        g_state.guiProbes[g_state.guiProbeCount].method = m;
                        g_state.guiProbes[g_state.guiProbeCount].name = getter;
                        g_state.guiProbeCount++;
                        Logger::Instance().Info("GuiManager probe found: %s", getter);
                    }
                }
                Logger::Instance().Info("GuiManager: %d boolean probes found", g_state.guiProbeCount);
            }
        }
    }

    // Camera helper methods for identity tracking
    auto camType = tdb->find_type("via.Camera");
    if (camType) g_state.getGameObject = camType->find_method("get_GameObject");
    auto goType = tdb->find_type("via.GameObject");
    if (goType) {
        g_state.getTransform = goType->find_method("get_Transform");
        g_state.getName = goType->find_method("get_Name");
    }

    Logger::Instance().Info("=== End RE9 type/method discovery ===");
}

void RefreshGameState() {
    uint64_t now = GetTickCount64();
    if (now - g_state.lastCheckTime < g_state.CHECK_INTERVAL_MS) return;
    g_state.lastCheckTime = now;

    const auto api = reframework::API::get().get();
    if (!api) {
        g_state.inGameplay = false;
        return;
    }

    if (!g_state.typesInitialized) {
        g_state.typesInitialized = true;
        DiscoverTypes();
    }

    bool newState = false;
    bool diag = (g_state.diagBurstRemaining > 0);
    if (diag) g_state.diagBurstRemaining--;

    do {
        // Tier 1: Camera exists?
        if (!g_state.getMainView || !g_state.getPrimaryCamera) break;
        auto sceneManager = api->get_native_singleton("via.SceneManager");
        if (!sceneManager) break;
        auto vmCtx = api->get_vm_context();
        auto mainView = g_state.getMainView->call<void*>(vmCtx, sceneManager);
        if (!mainView) break;
        auto camera = g_state.getPrimaryCamera->call<void*>(vmCtx, mainView);
        if (!camera) break;

        if (diag && g_state.getGameObject) {
            __try {
                auto camGO = g_state.getGameObject->call<void*>(vmCtx, camera);
                Logger::Instance().Info("Diag: cameraGO=%p (gameplay=%p, locked=%d)",
                    camGO, g_state.gameplayCameraGO, g_state.gameplayCameraLocked ? 1 : 0);
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }

        // Tier 1.5: GlobalSpeed
        if (g_state.getGlobalSpeed) {
            __try {
                auto app = api->get_native_singleton("via.Application");
                if (app) {
                    float speed = g_state.getGlobalSpeed->call<float>(vmCtx, app);
                    if (diag) Logger::Instance().Info("Diag: GlobalSpeed=%.3f", speed);
                    if (speed <= 0.001f) break;
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                g_state.getGlobalSpeed = nullptr;
            }
        }

        // Tier 2: SituationType (RE9: -1=Normal, 0=CutScene)
        if (g_state.situationType.method && !g_state.situationType.failed) {
            void* guiMgr = api->get_managed_singleton(g_state.situationType.singletonName);
            if (guiMgr) {
                __try {
                    auto ret = g_state.situationType.method->invoke(
                        reinterpret_cast<reframework::API::ManagedObject*>(guiMgr), ref::EmptyArgs());
                    int32_t sitType = static_cast<int32_t>(ret.dword);
                    if (diag) Logger::Instance().Info("Diag: situationType=%d", sitType);
                    if (sitType >= 0) break;
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    g_state.situationType.failed = true;
                }
            }
        }

        // Tier 2: All discovered bool checks
        if (ref::InvokeBool(api, vmCtx, g_state.isPaused, diag, "isPaused")) break;
        if (ref::InvokeBool(api, vmCtx, g_state.isPlayingEvent, diag, "isPlayingEvent")) break;
        if (ref::InvokeBool(api, vmCtx, g_state.isOpen, diag, "isOpen")) break;
        if (ref::InvokeBool(api, vmCtx, g_state.isSystemFlow, diag, "isSystemFlow")) break;
        if (ref::InvokeBool(api, vmCtx, g_state.isEventFlow, diag, "isEventFlow")) break;
        if (ref::InvokeBool(api, vmCtx, g_state.isPlaying, diag, "isPlaying")) break;
        if (ref::InvokeBool(api, vmCtx, g_state.isTransition, diag, "isTransition")) break;
        if (ref::InvokeBool(api, vmCtx, g_state.isEventPlaying, diag, "isEventPlaying")) break;
        if (ref::InvokeBool(api, vmCtx, g_state.isMoviePlaying, diag, "isMoviePlaying")) break;
        if (ref::InvokeBool(api, vmCtx, g_state.isInputBlocked, diag, "isInputBlocked")) break;
        if (ref::InvokeBool(api, vmCtx, g_state.isCutscene, diag, "isCutscene")) break;

        // Tier 2: Int checks
        if (ref::InvokeInt(api, vmCtx, g_state.pauseBits, diag, "pauseBits") != 0) break;

        if (g_state.flowStatus.method && !g_state.flowStatus.failed) {
            uint32_t status = ref::InvokeInt(api, vmCtx, g_state.flowStatus, diag, "flowStatus");
            if (g_state.flowStatus.method && status < 2) break;
        }

        // Tier 2: Player context
        if (!ref::InvokePointer(api, vmCtx, g_state.playerContext, diag, "playerCtx")) break;

        // Tier 2: GuiOpenCloseData -> CurrActiveInputLevel
        if (g_state.guiOpenClose.method && g_state.inputLevel.method &&
            !g_state.guiOpenClose.failed && !g_state.inputLevel.failed && g_state.guiSingletonName) {
            __try {
                void* guiMgr = api->get_managed_singleton(g_state.guiSingletonName);
                if (guiMgr) {
                    auto ocd = g_state.guiOpenClose.method->invoke(
                        reinterpret_cast<reframework::API::ManagedObject*>(guiMgr), ref::EmptyArgs());
                    if (ocd.ptr) {
                        auto lvl = g_state.inputLevel.method->invoke(
                            reinterpret_cast<reframework::API::ManagedObject*>(ocd.ptr), ref::EmptyArgs());
                        if (diag) Logger::Instance().Info("Diag: inputLevel=%u", lvl.dword);
                        if (lvl.dword > 0) break;
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                g_state.guiOpenClose.failed = true;
                Logger::Instance().Warning("GuiOpenCloseData chain crashed, disabling");
            }
        }

        // Cursor visibility (diagnostic only — NOT suppressing)
        {
            CURSORINFO ci = {};
            ci.cbSize = sizeof(ci);
            if (GetCursorInfo(&ci)) {
                bool cursorVisible = (ci.flags & CURSOR_SHOWING) != 0;
                if (cursorVisible) {
                    g_state.cursorVisibleCount++;
                    g_state.cursorHiddenCount = 0;
                } else {
                    g_state.cursorHiddenCount++;
                    if (g_state.cursorHiddenCount >= g_state.CURSOR_RESUME_THRESHOLD) {
                        g_state.cursorVisibleCount = 0;
                        g_state.cursorSuppressing = false;
                    }
                }
                if (g_state.cursorVisibleCount >= g_state.CURSOR_SUPPRESS_THRESHOLD) {
                    g_state.cursorSuppressing = true;
                }
                if (diag) Logger::Instance().Info("Diag: cursor=%d vis=%d hid=%d suppress=%d (NOT SUPPRESSING)",
                    cursorVisible ? 1 : 0, g_state.cursorVisibleCount,
                    g_state.cursorHiddenCount, g_state.cursorSuppressing ? 1 : 0);
            }
        }

        // Dump GuiManager probes during diagnostic bursts
        if (diag && g_state.guiManagerSingleton && g_state.guiProbeCount > 0) {
            void* guiMgr = api->get_managed_singleton(g_state.guiManagerSingleton);
            if (guiMgr) {
                for (int i = 0; i < g_state.guiProbeCount; i++) {
                    __try {
                        auto ret = g_state.guiProbes[i].method->invoke(
                            reinterpret_cast<reframework::API::ManagedObject*>(guiMgr), ref::EmptyArgs());
                        Logger::Instance().Info("Diag: %s = %u", g_state.guiProbes[i].name, ret.dword);
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                }
            }
        }

        newState = true;
    } while (false);

    g_state.inGameplay = newState;

    // Lock gameplay camera identity
    if (newState && g_state.getGameObject && !g_state.gameplayCameraLocked) {
        auto sceneManager2 = api->get_native_singleton("via.SceneManager");
        if (sceneManager2) {
            auto vmCtx2 = api->get_vm_context();
            __try {
                auto mv = g_state.getMainView->call<void*>(vmCtx2, sceneManager2);
                if (mv) {
                    auto cam = g_state.getPrimaryCamera->call<void*>(vmCtx2, mv);
                    if (cam) {
                        auto go = g_state.getGameObject->call<void*>(vmCtx2, cam);
                        if (go) {
                            g_state.gameplayCameraGO = go;
                            g_state.gameplayCameraLocked = true;
                            Logger::Instance().Info("Gameplay camera locked: GO=%p", go);
                        }
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    // Detect transitions
    if (g_state.inGameplay && !g_state.wasInGameplay) {
        g_state.pendingRecenter = true;
        g_state.diagBurstRemaining = 5;
        Logger::Instance().Info("Game state: entered gameplay — pending recenter");
    } else if (!g_state.inGameplay && g_state.wasInGameplay) {
        g_state.diagBurstRemaining = 5;
        Logger::Instance().Info("Game state: left gameplay");
        OnLeftGameplay();
    }
    g_state.wasInGameplay = g_state.inGameplay;
}

bool IsInGameplay() {
    RefreshGameState();
    return g_state.inGameplay;
}

bool ShouldRecenter() {
    if (g_state.pendingRecenter) {
        g_state.pendingRecenter = false;
        return true;
    }
    return false;
}

void TriggerGameStateDiag() {
    g_state.diagBurstRemaining = 10;
    Logger::Instance().Info("=== MARKER (F9) === inGameplay=%d, dumping next 10 checks", g_state.inGameplay ? 1 : 0);
}

} // namespace RE9HT
