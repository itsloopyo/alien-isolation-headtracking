#include "hud_prompt.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "build_profile.h"

#include "cameraunlock/logging/file_log.h"

// ===========================================================================
// The game's interaction prompt - the "E USE" glyph and the cursor dot that
// fades in with an interactable - is authored at the centre of the PickupOverlay
// Scaleform movie. With aim decoupled from view it belongs at the aim point.
//
// PickupOverlay is ActionScript 3 and exposes no method that repositions either
// clip, and this build has no path-based SetVariable, so nothing inside the
// movie can be driven. What can be driven is where the movie is drawn: its
// Scaleform player holds a GFx viewport whose origin decides the on-screen
// position of everything the movie renders. Moving that origin needs no VM
// call, no display-list access and no game state, and the engine rewrites it
// only on a resolution change.
//
// Movies are reached through a UI manager singleton holding named overlays.
// ===========================================================================

namespace {
using namespace cameraunlock;

// The overlay that owns the interaction prompt, confirmed at runtime by
// watching which overlay receives Set_Interaction_Prompt_Visible.
const char kPromptOverlay[] = "pickupOverlay";

// A manager holding more records than this is not the one we are looking for,
// so the walk stops rather than following whatever the pointer really is.
constexpr unsigned kMaxOverlays = 256;

// Where the engine put the viewport before we touched it. Keyed by player so a
// reload picks up a fresh baseline rather than compounding an old offset.
const void* g_basePlayer = nullptr;
int g_baseLeft = 0;
int g_baseTop = 0;

int g_lastOverlayCount = -1;

void* ReadPtr(const void* p, uintptr_t off) {
    return *reinterpret_cast<void* const*>(reinterpret_cast<const char*>(p) + off);
}

int& Field(void* obj, uintptr_t off) {
    return *reinterpret_cast<int*>(static_cast<char*>(obj) + off);
}

// Every address this walk uses was derived from one AI.exe. On any other build
// it reads unrelated data and then takes a lock on whatever sits inside it,
// which hangs or corrupts the render thread - so the prompt follows the same
// dormancy contract as the hooks.
//
// Kept out of Update(): a function-local static with a dynamic initialiser
// cannot share a function with __try (C2712).
bool BuildIsKnown() {
    static const bool known = builds::MatchesKnownBuild();
    static bool logged = false;
    if (!known && !logged) {
        logged = true;
        logging::Line("hud: unknown AI.exe build; interaction prompt left where the game draws it");
    }
    return known;
}

}  // namespace

namespace hud_prompt {

// ndc is the clean aim direction projected into the head-tracked view, the same
// point our own reticle is drawn at, so the prompt and the reticle stay
// together. Overlay records are only read while the manager's own lock is held
// and the pointer is still in its list - caching them across frames crashes on
// level load, when overlays churn.
void Update(float ndcX, float ndcY, bool valid) {
    if (!BuildIsKnown()) return;

    const builds::OffsetTable::Ui& ui = builds::ActiveOffsets().ui;
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    void* mgr = *reinterpret_cast<void**>(base + ui.manager_ptr);
    if (!mgr) return;

    CRITICAL_SECTION* cs =
        reinterpret_cast<CRITICAL_SECTION*>(static_cast<char*>(mgr) + ui.manager_lock);
    EnterCriticalSection(cs);
    __try {
        void** arr = static_cast<void**>(ReadPtr(mgr, ui.manager_overlay_array));
        const unsigned total =
            *reinterpret_cast<unsigned*>(static_cast<char*>(mgr) + ui.manager_overlay_count);
        if (arr && total <= kMaxOverlays) {
            const bool announce = static_cast<int>(total) != g_lastOverlayCount;
            for (unsigned i = 0; i < total; ++i) {
                void* o = arr[i];
                if (!o) continue;
                const char* n = static_cast<const char*>(ReadPtr(o, ui.overlay_name));
                if (announce) logging::Line("hud: overlay '%s'", n ? n : "(null)");
                if (!n || strcmp(n, kPromptOverlay) != 0) continue;

                void* player = ReadPtr(o, ui.overlay_player);
                if (!player) continue;
                if (player != g_basePlayer) {
                    g_basePlayer = player;
                    g_baseLeft = Field(player, ui.viewport_left);
                    g_baseTop = Field(player, ui.viewport_top);
                }

                if (!valid) {
                    Field(player, ui.viewport_left) = g_baseLeft;
                    Field(player, ui.viewport_top) = g_baseTop;
                    continue;
                }
                const float halfW = Field(player, ui.viewport_width) * 0.5f;
                const float halfH = Field(player, ui.viewport_height) * 0.5f;
                // NDC y points up, screen y points down.
                Field(player, ui.viewport_left) = g_baseLeft + static_cast<int>(ndcX * halfW);
                Field(player, ui.viewport_top) = g_baseTop - static_cast<int>(ndcY * halfH);
            }
            g_lastOverlayCount = static_cast<int>(total);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    LeaveCriticalSection(cs);
}

}  // namespace hud_prompt
