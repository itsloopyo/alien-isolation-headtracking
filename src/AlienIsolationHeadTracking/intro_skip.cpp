#include "intro_skip.h"

#include <windows.h>

#include <cstring>

#include <MinHook.h>

#include "build_profile.h"
#include "config.h"

#include "cameraunlock/logging/file_log.h"

// The boot sequence checks a debug-flag lookup for "skip_frontend" and skips its
// splash screens when that returns non-zero. In the shipped build that lookup is
// a stripped stub that always answers no, so the skip exists and is simply
// switched off. Answering yes for this one name turns it back on.
//
// Off unless the player asks for it in the INI. Those splash screens are where
// the developer, publisher and rights-holder credits are shown, and this mod has
// no business suppressing them as a side effect of adding head tracking. It
// exists because it also saves half a minute per launch when testing, and that
// is not a reason to take the credits away from everyone else.
//
// The same stub is reused as a no-op frame-task callback, so the name is
// checked rather than answering yes to everything, and the read is guarded
// because in that role it is invoked with no argument at all.

namespace intro_skip {
namespace {

using namespace cameraunlock;

typedef int(__cdecl* DebugFlag_t)(const char*);

DebugFlag_t g_origDebugFlag = nullptr;

const char kSkipFrontendFlag[] = "skip_frontend";

int __cdecl DebugFlagDetour(const char* name) {
    __try {
        if (name && strcmp(name, kSkipFrontendFlag) == 0) return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return g_origDebugFlag(name);
}

}  // namespace

void Install() {
    if (!config::Get().skip_intro_movies) return;
    if (!builds::MatchesKnownBuild()) return;
    void* target = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) +
                                           builds::ActiveOffsets().hooks.debug_flag);
    if (MH_CreateHook(target, &DebugFlagDetour, reinterpret_cast<void**>(&g_origDebugFlag)) !=
            MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        logging::Line("intro: skip hook FAILED; splash screens will play");
        return;
    }
    logging::Line("intro: frontend skip enabled");
}

}  // namespace intro_skip
