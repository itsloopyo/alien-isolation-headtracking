#include <windows.h>

#include <atomic>
#include <string>

#include "cameraunlock/diagnostics/crash_handler.h"
#include "cameraunlock/input/chord_hotkeys.h"
#include "cameraunlock/input/hotkey_poller.h"
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

// Doctrine nav-cluster hotkeys, with the Ctrl+Shift+<letter> chord
// alternatives for keyboards without a nav cluster.
constexpr int kVkToggle = 0x23;        // End
constexpr int kVkTrackingMode = 0x21;  // Page Up
constexpr int kVkFrustum = 0x2D;       // Insert
constexpr int kVkInjectMode = 0x2E;    // Delete
constexpr int kChordToggle = 'Y';
constexpr int kChordTrackingMode = 'G';
constexpr int kChordYawMode = 'H';
constexpr int kChordFrustum = 'U';
constexpr int kChordInjectMode = 'J';

// How long a launch goes without a single tracker packet before the log says so.
// Past the point where a tracker started alongside the game would have sent one.
constexpr int kNoTrackerReportSeconds = 30;

void DoToggleTracking() {
    bool on = !g_trackingEnabled.load();
    g_trackingEnabled.store(on);
#if defined(AIHT_CAMERA)
    camera::SetEnabled(on);
#endif
    logging::Line("Tracking %s", on ? "ENABLED" : "DISABLED");
}

void DoToggleInjectMode() {
#if defined(AIHT_CAMERA)
    logging::Line("Injection mode: %s", camera::CycleInjectionMode());
#endif
}

void DoToggleYawMode() {
#if defined(AIHT_CAMERA)
    camera::ToggleYawMode();
#endif
}

void DoToggleFrustum() {
#if defined(AIHT_CAMERA)
    const bool wide = !camera::IsFrustumWidening();
    camera::SetFrustumWidening(wide);
    logging::Line("Frustum widening %s", wide ? "ON (engine culls wider, view zooms out)"
                                              : "OFF");
#endif
}

void DoCycleTrackingMode() {
#if defined(AIHT_CAMERA)
    logging::Line("Tracking mode: %s", camera::CycleTrackingMode());
#else
    static const char* const kModes[] = {"ROTATION AND POSITION", "ROTATION ONLY (position off)",
                                         "POSITION ONLY (rotation off)"};
    static int mode = 0;
    mode = (mode + 1) % 3;
    logging::Line("Tracking mode: %s", kModes[mode]);
#endif
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

void InitThread() {
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

    g_receiver.SetLog([](const std::string& msg) { logging::Line("[udp] %s", msg.c_str()); });
    if (g_receiver.Start(UdpReceiver::kDefaultPort)) {
        logging::Line("UDP receiver listening on %u", UdpReceiver::kDefaultPort);
    } else {
        logging::Line("UDP receiver not bound yet (retry loop active)");
    }

    g_hotkeys.AddHotkey(kVkToggle, input::NavGuarded(&DoToggleTracking));
    g_hotkeys.AddHotkey(kVkTrackingMode, input::NavGuarded(&DoCycleTrackingMode));
    g_hotkeys.AddHotkey(config::Get().yaw_mode_key, input::NavGuarded(&DoToggleYawMode));
    g_hotkeys.AddHotkey(kVkFrustum, input::NavGuarded(&DoToggleFrustum));
    g_hotkeys.AddHotkey(kVkInjectMode, input::NavGuarded(&DoToggleInjectMode));
    g_hotkeys.AddHotkey(kChordToggle, input::ChordGuarded(&DoToggleTracking));
    g_hotkeys.AddHotkey(kChordTrackingMode, input::ChordGuarded(&DoCycleTrackingMode));
    g_hotkeys.AddHotkey(kChordYawMode, input::ChordGuarded(&DoToggleYawMode));
    g_hotkeys.AddHotkey(kChordFrustum, input::ChordGuarded(&DoToggleFrustum));
    g_hotkeys.AddHotkey(kChordInjectMode, input::ChordGuarded(&DoToggleInjectMode));
    g_hotkeys.Start(16);
    logging::Line("Hotkeys: End=toggle PageUp=tracking-mode "
                  "PageDown=yaw-mode Insert=frustum-widen Delete=injection-mode "
                  "(chords: Ctrl+Shift+Y/G/H/U/J)");

    // On its own thread: it waits for the window to exist, and the hook install
    // below must not be delayed behind that (the Steam overlay hooks the same
    // D3D entry points, so installing late can lose the race).
    HANDLE centering = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        CenterGameWindow();
        return 0;
    }, nullptr, 0, nullptr);
    if (centering) CloseHandle(centering);

#if defined(AIHT_CAMERA)
    camera::Install(g_receiver);
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
                          UdpReceiver::kDefaultPort);
        }
        if (receiving == wasReceiving) continue;
        wasReceiving = receiving;
        logging::Line("Tracker %s%s", receiving ? "CONNECTED" : "lost",
                      (receiving && g_receiver.IsRemoteConnection()) ? " (remote)" : "");
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
