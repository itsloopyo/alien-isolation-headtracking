#include <windows.h>

#include <atomic>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/diagnostics/crash_handler.h"
#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"
#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/memory/pe_fingerprint.h"
#include "cameraunlock/protocol/udp_receiver.h"

#include "config.h"
#include "version.h"

#if defined(AIHT_CAMERA)
#include "camera_hook.h"
#endif

namespace {

using namespace cameraunlock;

UdpReceiver g_receiver;
input::HotkeyPoller g_hotkeys;

std::atomic<bool> g_trackingEnabled{true};

// The one reader and writer of CameraUnlock.ini, never destroyed: the hotkey
// thread saves through it for as long as the process runs. Built and loaded on
// the init thread before the hotkeys start.
config::ConfigOwner<aiht::Config>* g_configOwner = nullptr;

// How long a launch goes without a single tracker packet before the log says so.
// Past the point where a tracker started alongside the game would have sent one.
constexpr int kNoTrackerReportSeconds = 30;

void DoToggleTracking() {
    bool on = !g_trackingEnabled.load();
    g_trackingEnabled.store(on);
#if defined(AIHT_CAMERA)
    ::camera::SetEnabled(on);
#endif
    logging::Line("Tracking %s", on ? "ENABLED" : "DISABLED");
}

void DoToggleInjectMode() {
#if defined(AIHT_CAMERA)
    logging::Line("Injection mode: %s", ::camera::CycleInjectionMode());
#endif
}

std::wstring ExeDirFileW(const wchar_t* name) {
    wchar_t exePath[MAX_PATH] = {};
    // A failure leaves the buffer untouched and a truncated result is not
    // guaranteed to be terminated, so the length has to be checked before the
    // buffer is read as a string. Either way the process directory is the only
    // place left to write, and the log cannot report its own absence.
    const DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return std::wstring(L".\\") + name;
    const std::wstring p(exePath, len);
    const size_t slash = p.find_last_of(L"\\/");
    const std::wstring dir = (slash == std::wstring::npos) ? L"." : p.substr(0, slash);
    return dir + L"\\" + name;
}

// A save that did not happen has already reached the log through the status
// sink; the session keeps the state the toggle applied. A save that did can
// carry a line too, naming a row that stopped following Defaults.ini.
void LogSave(const config::ConfigSaveResult& saved) {
    for (const std::string& line : saved.log) logging::Line("%s", line.c_str());
    if (saved.status != config::ConfigSaveStatus::Saved) {
        logging::Line("config: the change applies for this session only");
    }
}

// Each toggle applies its new state first, then saves it. End, the frustum
// widening and the injection mode are not saved: they change the session only.
void DoToggleYawMode() {
#if defined(AIHT_CAMERA)
    const bool world = ::camera::ToggleYawMode();
#else
    static std::atomic<bool> yaw{aiht::Settings().world_space_yaw};
    const bool world = !yaw.load();
    yaw.store(world);
#endif
    LogSave(g_configOwner->Save([world](aiht::Config& c) { c.world_space_yaw = world; }));
}

void DoToggleFrustum() {
#if defined(AIHT_CAMERA)
    const bool wide = !::camera::IsFrustumWidening();
    ::camera::SetFrustumWidening(wide);
    logging::Line("Frustum widening %s", wide ? "ON (engine culls wider, view zooms out)"
                                              : "OFF");
#endif
}

const char* TrackingModeName(TrackingMode mode) {
    switch (mode) {
        case TrackingMode::RotationOnly: return "ROTATION ONLY (position off)";
        case TrackingMode::PositionOnly: return "POSITION ONLY (rotation off)";
        default: return "ROTATION AND POSITION";
    }
}

void DoCycleTrackingMode() {
#if defined(AIHT_CAMERA)
    const TrackingMode mode = ::camera::CycleTrackingMode();
#else
    static std::atomic<int> current{static_cast<int>(
        *DecodeTrackingMode(aiht::Settings().rotation_enabled, aiht::Settings().position_enabled))};
    const TrackingMode mode = static_cast<TrackingMode>((current.load() + 1) % 3);
    current.store(static_cast<int>(mode));
#endif
    logging::Line("Tracking mode: %s", TrackingModeName(mode));
    const TrackingModeChannels channels = EncodeTrackingMode(mode);
    LogSave(g_configOwner->Save([channels](aiht::Config& c) {
        c.rotation_enabled = channels.rotation_enabled;
        c.position_enabled = channels.position_enabled;
    }));
}

// The config table already refused a list that does not parse, so one here is a
// bug rather than a player's typo.
std::vector<input::KeyBinding> Bindings(const std::string& list) {
    input::KeyBindingsParseResult parsed = input::ParseKeyBindings(list);
    if (!parsed.ok()) throw std::logic_error("hotkey list '" + list + "' does not parse: " + parsed.error);
    return parsed.bindings;
}

void RegisterHotkeys(const aiht::Config& cfg) {
    // One registration per key: a binding without modifiers stays quiet while
    // Ctrl and Shift are both held, so Ctrl+Shift with a key reaches only a
    // binding that names it, and one press never fires an action twice.
    input::RegisterKeyBindings(g_hotkeys, Bindings(cfg.toggle_key_name), &DoToggleTracking);
    input::RegisterKeyBindings(g_hotkeys, Bindings(cfg.cycle_tracking_mode_key_name), &DoCycleTrackingMode);
    input::RegisterKeyBindings(g_hotkeys, Bindings(cfg.yaw_mode_key_name), &DoToggleYawMode);
    input::RegisterKeyBindings(g_hotkeys, Bindings(cfg.frustum_widening_key_name), &DoToggleFrustum);
    input::RegisterKeyBindings(g_hotkeys, Bindings(cfg.injection_mode_key_name), &DoToggleInjectMode);
    g_hotkeys.Start(16);
    logging::Line("Hotkeys: toggle=[%s] tracking-mode=[%s] yaw-mode=[%s] frustum-widen=[%s] "
                  "injection-mode=[%s]",
                  cfg.toggle_key_name.c_str(), cfg.cycle_tracking_mode_key_name.c_str(),
                  cfg.yaw_mode_key_name.c_str(), cfg.frustum_widening_key_name.c_str(),
                  cfg.injection_mode_key_name.c_str());
}

// Loads CameraUnlock.ini beside AI.exe, importing AlienIsolationHeadTracking.ini
// once when it is not there yet, and publishes the settings for the session.
void LoadConfig() {
    const std::wstring exeFile = ExeDirFileW(L"");
    config::ConfigOwnerOptions<aiht::Config> options =
        aiht::MakeConfigOwnerOptions(exeFile, config::DefaultsFile::PerUser());
    // The log is the only place this mod can tell the player anything.
    options.status_sink = [](const std::string& message) { logging::Line("config: %s", message.c_str()); };
    g_configOwner = new config::ConfigOwner<aiht::Config>(std::move(options));
    const config::ConfigLoadResult<aiht::Config> loaded = g_configOwner->Load();
    for (const std::string& line : loaded.log) logging::Line("%s", line.c_str());
    logging::Line("config: %s", config::ConfigLoadStatusName(loaded.status));
    aiht::PublishSettings(loaded.config);
    const aiht::Config& cfg = aiht::Settings();
    logging::Line("config: port=%d enabled=%s WorldSpaceYaw=%s HelmetFollowsHead=%s SkipIntroMovies=%s "
                  "LocalSmoothing=%.2f RemoteSmoothing=%.2f",
                  cfg.udp_port, cfg.enable_on_startup ? "true" : "false",
                  cfg.world_space_yaw ? "true" : "false", cfg.helmet_follows_head ? "true" : "false",
                  cfg.skip_intro_movies ? "true" : "false", cfg.local_smoothing, cfg.remote_smoothing);
}

struct MainWindowSearch {
    DWORD pid;
    HWND hwnd;
};

BOOL CALLBACK FindMainWindow(HWND hwnd, LPARAM param) {
    auto* search = reinterpret_cast<MainWindowSearch*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != search->pid || GetWindow(hwnd, GW_OWNER) || !IsWindowVisible(hwnd)) return TRUE;
    search->hwnd = hwnd;
    return FALSE;
}

// Centres the game window on its monitor. The game places itself top-left,
// which puts it in a corner on an ultrawide. A window already covering the
// whole monitor is borderless or exclusive fullscreen and is left alone.
void CenterGameWindow() {
    MainWindowSearch search{GetCurrentProcessId(), nullptr};
    for (int attempt = 0; attempt < 60 && !search.hwnd; ++attempt) {
        EnumWindows(&FindMainWindow, reinterpret_cast<LPARAM>(&search));
        if (!search.hwnd) Sleep(500);
    }
    if (!search.hwnd) {
        logging::Line("window: main window not found, leaving position alone");
        return;
    }

    RECT window{};
    if (!GetWindowRect(search.hwnd, &window)) return;
    const int width = window.right - window.left;
    const int height = window.bottom - window.top;

    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(MonitorFromWindow(search.hwnd, MONITOR_DEFAULTTONEAREST), &mi)) return;
    const RECT& work = mi.rcWork;
    const int workWidth = work.right - work.left;
    const int workHeight = work.bottom - work.top;

    if (width >= mi.rcMonitor.right - mi.rcMonitor.left &&
        height >= mi.rcMonitor.bottom - mi.rcMonitor.top) {
        logging::Line("window: fullscreen, leaving position alone");
        return;
    }

    const int x = work.left + (workWidth - width) / 2;
    const int y = work.top + (workHeight - height) / 2;
    SetWindowPos(search.hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    logging::Line("window: centred %dx%d at %d,%d", width, height, x, y);
}

void InitThreadBody() {
    // Open() truncates this launch's log and files the outgoing one away as
    // HeadTracking.prev.log, so the log is this session's alone and two
    // generations are all that ever accumulate next to the EXE. The previous
    // generation is kept because the crash handler writes its report here and
    // the relaunch the player makes to go and read it would otherwise destroy it.
    logging::Open(ExeDirFileW(L"HeadTracking.log"));
    logging::Line("=== %s v%s ===", AIHT_NAME, AIHT_VERSION);
    diagnostics::InstallCrashHandler();

    memory::PeFingerprint fp{};
    if (memory::ReadPeFingerprint(GetModuleHandleW(nullptr), fp)) {
        logging::Line("AI.exe fingerprint: TimeDateStamp=0x%08X SizeOfImage=0x%08X CheckSum=0x%08X",
                      fp.TimeDateStamp, fp.SizeOfImage, fp.CheckSum);
    } else {
        logging::Line("WARNING: could not read AI.exe PE fingerprint");
    }

    LoadConfig();
    const aiht::Config& cfg = aiht::Settings();
    g_trackingEnabled.store(cfg.enable_on_startup);
    const unsigned short port = static_cast<unsigned short>(cfg.udp_port);

    g_receiver.SetLog([](const std::string& msg) { logging::Line("[udp] %s", msg.c_str()); });
    if (g_receiver.Start(port)) {
        logging::Line("UDP receiver listening on %u", port);
    } else {
        logging::Line("UDP receiver not bound yet (retry loop active)");
    }

    // On its own thread: it waits for the window to exist, and the hook install
    // below must not be delayed behind that (the Steam overlay hooks the same
    // D3D entry points, so installing late can lose the race).
    HANDLE centering = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        CenterGameWindow();
        return 0;
    }, nullptr, 0, nullptr);
    if (centering) CloseHandle(centering);

#if defined(AIHT_CAMERA)
    ::camera::StartSession(g_receiver);
#endif
    // After the session the mode hotkey cycles, and before the hook install,
    // which can block for fifteen seconds.
    RegisterHotkeys(cfg);
#if defined(AIHT_CAMERA)
    ::camera::Install();
#endif

    // Tracker state, on change only. The live pose is reported from the render
    // side, which knows whether the camera is being driven with it; a second
    // periodic pose line here said the same thing twice and was half the log.
    bool wasReceiving = false;
    bool everReceived = false;
    int seconds = 0;
    for (;;) {
        Sleep(1000);
        const bool receiving = g_receiver.IsReceiving();
        everReceived = everReceived || receiving;
        // Said outright, because the reason a log has no tracking in it is
        // usually that nothing ever arrived, and that reads as silence.
        if (!everReceived && ++seconds == kNoTrackerReportSeconds) {
            logging::Line("No tracker packets on UDP %u yet. Start OpenTrack (or the phone app) "
                          "and point its output at this machine on that port.",
                          port);
        }
        if (receiving == wasReceiving) continue;
        wasReceiving = receiving;
        logging::Line("Tracker %s%s", receiving ? "CONNECTED" : "lost",
                      (receiving && g_receiver.IsRemoteConnection()) ? " (remote)" : "");
    }
}

// An exception escaping a bare thread procedure is std::terminate, the game
// dying with the log stopping mid-startup. The config owner throws for a table
// it cannot render or a path it cannot use, and the poller for a thread it
// cannot start.
void InitThread() {
    try {
        InitThreadBody();
    } catch (const std::exception& e) {
        logging::Line("ERROR: startup failed: %s", e.what());
    }
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE t = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            InitThread();
            return 0;
        }, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
