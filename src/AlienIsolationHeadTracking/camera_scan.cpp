#include "camera_scan.h"

#include <windows.h>
#include <tlhelp32.h>
#include <cmath>
#include <cstdint>

#include "cameraunlock/logging/file_log.h"

namespace camera_scan {
namespace {

using cameraunlock::logging::Line;
// Line() serialises on a mutex. Inside the vectored handler that is a deadlock
// waiting to happen: the trapped thread may already hold that mutex, and every
// other thread that wants it is blocked behind a thread we have stopped. The
// core library ships EmergencyLine for exactly this - lock-free, no CRT, no
// heap - so the writer probe reports through it.
using cameraunlock::logging::EmergencyLine;

constexpr int kMaxCandidates = 8192;
// The engine's stored position and the one recovered from the view matrix go
// through different arithmetic, so they agree closely rather than exactly.
constexpr float kMatchEpsilon = 1e-3f;
// How far the watched vector must move before a filtering pass tells us
// anything: narrowing on a value that has barely changed drops nothing.
constexpr float kMinChange = 0.08f;

uintptr_t g_candidates[kMaxCandidates];
int g_candidateCount = 0;

enum Phase { kWaiting, kScanning, kNarrowing, kWatching, kSettled };
Phase g_phase = kWaiting;

// The first pass walks the whole address space, which takes seconds. It runs on
// a worker thread because the caller is the render thread, and stalling that
// would show up as the game hanging.
float g_scanPos[3];
volatile LONG g_scanDone = 0;

float g_lastScanPos[3] = {0.0f, 0.0f, 0.0f};
bool g_haveLastScanPos = false;
int g_passes = 0;
int g_settledRepeats = 0;

bool ReadsAsPosition(const float* p, const float* pos) {
    return fabsf(p[0] - pos[0]) < kMatchEpsilon && fabsf(p[1] - pos[1]) < kMatchEpsilon &&
           fabsf(p[2] - pos[2]) < kMatchEpsilon;
}

bool RegionIsScannable(const MEMORY_BASIC_INFORMATION& mbi) {
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & PAGE_GUARD) return false;
    if (mbi.Protect & PAGE_NOACCESS) return false;
    const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE |
                           PAGE_EXECUTE_WRITECOPY;
    return (mbi.Protect & writable) != 0;
}

void ScanRegion(const MEMORY_BASIC_INFORMATION& mbi, const float* pos) {
    __try {
        const float* base = static_cast<const float*>(mbi.BaseAddress);
        const size_t floats = mbi.RegionSize / sizeof(float);
        for (size_t i = 0; i + 3 <= floats && g_candidateCount < kMaxCandidates; ++i) {
            if (!ReadsAsPosition(base + i, pos)) continue;
            g_candidates[g_candidateCount++] =
                reinterpret_cast<uintptr_t>(base) + i * sizeof(float);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // A region can be freed underneath us mid-scan; skip it and continue.
    }
}

void FullScan(const float* pos) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t addr = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    const uintptr_t end = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);

    MEMORY_BASIC_INFORMATION mbi;
    while (addr < end && g_candidateCount < kMaxCandidates) {
        if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) != sizeof(mbi)) break;
        if (RegionIsScannable(mbi)) ScanRegion(mbi, pos);
        addr = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }
}

bool StillMatches(uintptr_t addr, const float* pos) {
    __try {
        return ReadsAsPosition(reinterpret_cast<const float*>(addr), pos);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Narrow(const float* pos) {
    int keep = 0;
    for (int i = 0; i < g_candidateCount; ++i)
        if (StillMatches(g_candidates[i], pos)) g_candidates[keep++] = g_candidates[i];
    g_candidateCount = keep;
}

void ArmWriteWatchOn(uintptr_t addr, const char* label);
void ReportSurvivors() {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    Line("camscan: %d address(es) track the camera position:", g_candidateCount);
    for (int i = 0; i < g_candidateCount && i < 32; ++i) {
        const uintptr_t a = g_candidates[i];
        MEMORY_BASIC_INFORMATION mbi;
        const bool inImage = VirtualQuery(reinterpret_cast<LPCVOID>(a), &mbi, sizeof(mbi)) &&
                             mbi.Type == MEM_IMAGE;
        if (inImage && a > base)
            Line("  0x%08X  AI.exe+0x%X", static_cast<unsigned>(a),
                 static_cast<unsigned>(a - base));
        else
            Line("  0x%08X  (heap)", static_cast<unsigned>(a));
    }
}

// --- who writes the camera ------------------------------------------------
//
// Once the storage is located, the writer is found by asking the CPU rather
// than by reading disassembly: a hardware write breakpoint on the address, and
// a vectored exception handler that records the instruction that tripped it.
// The faulting address, minus the module base, is the RVA to decompile and
// hook.

// The render camera object sits at AI.exe+0x1357E40. FUN_0097a100 reads these
// fields transposed and uploads them, so they are computed earlier by someone
// else - and that someone is what source injection has to hook. Watch the
// derived view matrix, the look-at inputs, and the view-projection at once;
// x86 gives four data breakpoints, which is exactly enough.
struct WatchSlot {
    uintptr_t rva;
    const char* name;
};
const WatchSlot kWatchSlots[4] = {
    {0x13580C0, "view matrix"},
    {0x1358110, "look-at target"},
    {0x1358120, "up vector"},
    {0x1357E40, "view-projection"},
};

bool g_watchReads = false;
bool (*g_isRotated)() = nullptr;
const char* g_watchLabel = "";

uintptr_t g_watchAddress[4] = {0, 0, 0, 0};
uintptr_t g_writers[16];
int g_writerSlot[16];
int g_writerCount = 0;
PVOID g_vehHandle = nullptr;

void ForEachOtherThread(void (*apply)(HANDLE)) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{sizeof(te)};
    const DWORD pid = GetCurrentProcessId();
    const DWORD self = GetCurrentThreadId();
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
            HANDLE h = OpenThread(
                THREAD_SET_CONTEXT | THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE,
                te.th32ThreadID);
            if (!h) continue;
            SuspendThread(h);
            apply(h);
            ResumeThread(h);
            CloseHandle(h);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

void ArmThread(HANDLE h) {
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(h, &ctx)) return;
    ctx.Dr0 = g_watchAddress[0];
    ctx.Dr1 = g_watchAddress[1];
    ctx.Dr2 = g_watchAddress[2];
    ctx.Dr3 = g_watchAddress[3];
    // Per slot i: local-enable at bit 2i, R/W = 01 (write) and LEN = 11 (four
    // bytes) in the control nibble at bit 16 + 4i.
    // R/W bits: 01 breaks on write, 11 on read or write.
    const DWORD rw = g_watchReads ? 3u : 1u;
    DWORD dr7 = 0;
    for (int i = 0; i < 4; ++i)
        if (g_watchAddress[i]) dr7 |= (1u << (2 * i)) | (rw << (16 + 4 * i)) | (3u << (18 + 4 * i));
    ctx.Dr7 = dr7;
    ctx.Dr6 = 0;
    SetThreadContext(h, &ctx);
}

void DisarmThread(HANDLE h) {
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(h, &ctx)) return;
    ctx.Dr0 = ctx.Dr1 = ctx.Dr2 = ctx.Dr3 = 0;
    ctx.Dr7 = 0;
    ctx.Dr6 = 0;
    SetThreadContext(h, &ctx);
}

// A write that lands in a memcpy thunk tells us nothing - dozens of callers
// share it. Recovering the caller means reading the stack: any dword pointing
// into AI.exe's code and preceded by a CALL is a return address, and the first
// few of those are the chain that led here.
bool LooksLikeReturnAddress(uintptr_t v, uintptr_t lo, uintptr_t hi) {
    if (v <= lo || v >= hi) return false;
    __try {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(v);
        if (p[-5] == 0xE8) return true;             // call rel32
        if (p[-2] == 0xFF) return true;             // call r/m32
        if (p[-3] == 0xFF) return true;
        if (p[-6] == 0xFF) return true;             // call [disp32]
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void LogCallers(const CONTEXT* ctx, uintptr_t base) {
    const uintptr_t lo = base + 0x1000;
    const uintptr_t hi = base + 0x01F81000;
    const uintptr_t* sp = reinterpret_cast<const uintptr_t*>(ctx->Esp);
    int shown = 0;
    for (int i = 0; i < 160 && shown < 5; ++i) {
        uintptr_t v = 0;
        __try {
            v = sp[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
        if (!LooksLikeReturnAddress(v, lo, hi)) continue;
        EmergencyLine("    caller AI.exe+0x%X", static_cast<unsigned>(v - base));
        ++shown;
    }
}

LONG CALLBACK OnWriteBreakpoint(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    // B0..B3 in Dr6 say which of our data breakpoints fired, if any; anything
    // else is an ordinary single step and is not ours.
    const DWORD hit = ep->ContextRecord->Dr6 & 0xF;
    if (!hit) return EXCEPTION_CONTINUE_SEARCH;

    int slot = 0;
    while (slot < 3 && !(hit & (1u << slot))) ++slot;

    const uintptr_t eip = ep->ContextRecord->Eip;
    bool seen = false;
    for (int i = 0; i < g_writerCount; ++i)
        if (g_writers[i] == eip && g_writerSlot[i] == slot) { seen = true; break; }
    if (!seen && g_writerCount < 16) {
        g_writers[g_writerCount] = eip;
        g_writerSlot[g_writerCount] = slot;
        ++g_writerCount;
        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (g_watchReads)
            EmergencyLine("camscan: %s read by AI.exe+0x%X  [camera %s]", g_watchLabel,
                          static_cast<unsigned>(eip - base),
                          (g_isRotated && g_isRotated()) ? "ROTATED" : "clean");
        else
            EmergencyLine("camscan: %s written by AI.exe+0x%X",
                          g_watchLabel[0] ? g_watchLabel : kWatchSlots[slot].name,
                          static_cast<unsigned>(eip - base));
        LogCallers(ep->ContextRecord, base);
    }
    ep->ContextRecord->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

void StartWriteWatch() {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    for (int i = 0; i < 4; ++i) g_watchAddress[i] = base + kWatchSlots[i].rva;
    g_vehHandle = AddVectoredExceptionHandler(1, &OnWriteBreakpoint);
    if (!g_vehHandle) {
        Line("camscan: could not install exception handler; no writer probe");
        return;
    }
    // Armed from a helper thread so that the caller's own thread gets a
    // breakpoint too: this runs on the render thread, and the camera may well
    // be written there.
    HANDLE t = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        ForEachOtherThread(&ArmThread);
        return 0;
    }, nullptr, 0, nullptr);
    if (t) { WaitForSingleObject(t, 2000); CloseHandle(t); }
    for (int i = 0; i < 4; ++i)
        Line("camscan: watching %s at AI.exe+0x%X", kWatchSlots[i].name,
             static_cast<unsigned>(kWatchSlots[i].rva));
}

void StopWriteWatch() {
    HANDLE t = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        ForEachOtherThread(&DisarmThread);
        return 0;
    }, nullptr, 0, nullptr);
    if (t) { WaitForSingleObject(t, 2000); CloseHandle(t); }
    if (g_vehHandle) RemoveVectoredExceptionHandler(g_vehHandle);
    g_vehHandle = nullptr;
    Line("camscan: writer probe done, %d distinct writer(s)", g_writerCount);
}

void ArmWriteWatchOn(uintptr_t addr, const char* label) {
    g_phase = kWatching;
    g_watchReads = false;
    g_watchLabel = label;
    g_writerCount = 0;
    g_watchAddress[0] = addr;
    g_watchAddress[1] = g_watchAddress[2] = g_watchAddress[3] = 0;

    if (!g_vehHandle) g_vehHandle = AddVectoredExceptionHandler(1, &OnWriteBreakpoint);
    if (!g_vehHandle) {
        Line("camscan: could not install exception handler; no writer probe");
        g_phase = kSettled;
        return;
    }
    HANDLE t = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        ForEachOtherThread(&ArmThread);
        return 0;
    }, nullptr, 0, nullptr);
    if (t) { WaitForSingleObject(t, 2000); CloseHandle(t); }
    Line("camscan: watching writes to the %s at 0x%08X", label, static_cast<unsigned>(addr));
}

}  // namespace

void SetRotationStateProbe(bool (*isRotated)()) { g_isRotated = isRotated; }

void WatchReads(void* address, const char* label) {
    if (g_phase != kWaiting || !address) return;
    g_phase = kWatching;
    g_watchReads = true;
    g_watchLabel = label;
    g_watchAddress[0] = reinterpret_cast<uintptr_t>(address);
    g_watchAddress[1] = g_watchAddress[2] = g_watchAddress[3] = 0;

    g_vehHandle = AddVectoredExceptionHandler(1, &OnWriteBreakpoint);
    if (!g_vehHandle) {
        Line("camscan: could not install exception handler; no reader probe");
        g_phase = kSettled;
        return;
    }
    HANDLE t = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        ForEachOtherThread(&ArmThread);
        return 0;
    }, nullptr, 0, nullptr);
    if (t) { WaitForSingleObject(t, 2000); CloseHandle(t); }
    Line("camscan: watching reads of %s", label);
}

void ScanForVector(const float* v) {
    if (g_phase == kSettled || g_watchReads || g_phase == kWatching) return;

    if (!g_haveLastScanPos) {
        for (int i = 0; i < 3; ++i) g_lastScanPos[i] = v[i];
        g_haveLastScanPos = true;
        return;
    }
    // A direction only tells us anything once it has changed appreciably.
    const float moved = sqrtf((v[0] - g_lastScanPos[0]) * (v[0] - g_lastScanPos[0]) +
                              (v[1] - g_lastScanPos[1]) * (v[1] - g_lastScanPos[1]) +
                              (v[2] - g_lastScanPos[2]) * (v[2] - g_lastScanPos[2]));
    if (moved < kMinChange) return;
    for (int i = 0; i < 3; ++i) g_lastScanPos[i] = v[i];

    if (g_phase == kWaiting) {
        for (int i = 0; i < 3; ++i) g_scanPos[i] = v[i];
        g_candidateCount = 0;
        g_scanDone = 0;
        g_phase = kScanning;
        Line("camscan: scanning for the rotated forward (%.3f %.3f %.3f)...", v[0], v[1], v[2]);
        HANDLE t = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            FullScan(g_scanPos);
            Line("camscan: first pass -> %d candidate(s)", g_candidateCount);
            InterlockedExchange(&g_scanDone, 1);
            return 0;
        }, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
        return;
    }
    if (g_phase == kScanning) {
        if (!InterlockedCompareExchange(&g_scanDone, 1, 1)) return;
        g_phase = kNarrowing;
        return;
    }

    const int before = g_candidateCount;
    Narrow(v);
    ++g_passes;
    Line("camscan: pass %d -> %d of %d survive", g_passes, g_candidateCount, before);
    if (g_candidateCount == 0) {
        Line("camscan: nothing caches the rotated forward");
        g_phase = kSettled;
        return;
    }
    if (g_candidateCount == before) ++g_settledRepeats;
    else g_settledRepeats = 0;
    if (g_candidateCount <= 24 && g_settledRepeats >= 2) {
        ReportSurvivors();
        // The cache is on the heap, so its address moves every launch and is
        // useless to hook directly. What we need is whatever WRITES it: that
        // has a stable RVA, and correcting the value there is what decouples
        // aim. Chain straight into a write breakpoint on the survivor.
        ArmWriteWatchOn(g_candidates[0], "cached forward");
    }
}

void Update() {
    if (g_phase == kSettled) return;

    if (g_watchReads) {
        if (g_phase == kWatching && (++g_passes >= 900 || g_writerCount >= 14)) {
            StopWriteWatch();
            g_phase = kSettled;
        }
        return;
    }

    if (g_phase == kWaiting) {
        StartWriteWatch();
        g_phase = g_vehHandle ? kWatching : kSettled;
        return;
    }

    // A few seconds of writes names every setter worth knowing; leave the
    // breakpoints armed no longer than that, since each write traps into us.
    if (++g_passes >= 600 || g_writerCount >= 12) {
        StopWriteWatch();
        g_phase = kSettled;
    }
}

}  // namespace camera_scan
