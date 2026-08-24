#include "hud_prompt.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "build_profile.h"

#include "cameraunlock/logging/file_log.h"

// ===========================================================================
// The HUD the game authors at the centre of the screen - the "E USE" glyph and
// its cursor dot, and the weapon reticles, including the flamethrower's corner
// brackets - marks where the shot goes. With aim decoupled from view that is no
// longer the centre of the picture, so it belongs at the aim point.
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

// The overlays whose contents are authored around the crosshair. `pickupOverlay`
// owns the interaction prompt, confirmed at runtime by watching which overlay
// receives Set_Interaction_Prompt_Visible; `weaponstuff` owns the weapon
// reticles - the flamethrower's bracket of corners is drawn there, which is why
// it stayed glued to the centre while everything else followed the aim.
//
// Their Scaleform players are separate objects, so each carries its own viewport
// and needs its own baseline. `pickupOverlay` shares a player with
// `popup_message`, so those two move together; that has always been the case.
const char* const kCentredOverlays[] = {"pickupOverlay", "weaponstuff"};
constexpr int kCentredOverlayCount =
    static_cast<int>(sizeof(kCentredOverlays) / sizeof(kCentredOverlays[0]));

bool IsCentredOverlay(const char* name) {
    for (int i = 0; i < kCentredOverlayCount; ++i)
        if (strcmp(name, kCentredOverlays[i]) == 0) return true;
    return false;
}

// A manager holding more records than this is not the one we are looking for,
// so the walk stops rather than following whatever the pointer really is.
constexpr unsigned kMaxOverlays = 256;

// Where the engine put each viewport before we touched it. Keyed by player so a
// reload picks up a fresh baseline rather than compounding an old offset, and
// one slot per overlay we move because they have separate players.
struct Baseline {
    const void* player = nullptr;
    int left = 0;
    int top = 0;
};
Baseline g_baselines[kCentredOverlayCount];

// The baseline slot for this player, seeded from the engine's own values the
// first time the player is seen. A player that has changed underneath us is a
// reload, and its slot is re-seeded rather than reused.
Baseline& BaselineFor(void* player, int slot, int left, int top) {
    Baseline& base = g_baselines[slot];
    if (base.player != player) {
        base.player = player;
        base.left = left;
        base.top = top;
    }
    return base;
}

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

// ndc is the clean aim point projected into the head-tracked view, the same
// point our own reticle is drawn at, so the prompt, the weapon reticle and the
// dot stay together. Overlay records are only read while the manager's own lock is held
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
            for (unsigned i = 0; i < total; ++i) {
                void* o = arr[i];
                if (!o) continue;
                const char* n = static_cast<const char*>(ReadPtr(o, ui.overlay_name));
                if (!n || !IsCentredOverlay(n)) continue;
                int slot = 0;
                while (strcmp(n, kCentredOverlays[slot]) != 0) ++slot;

                void* player = ReadPtr(o, ui.overlay_player);
                if (!player) continue;
                const Baseline& base = BaselineFor(player, slot, Field(player, ui.viewport_left),
                                                   Field(player, ui.viewport_top));

                if (!valid) {
                    Field(player, ui.viewport_left) = base.left;
                    Field(player, ui.viewport_top) = base.top;
                    continue;
                }
                const float halfW = Field(player, ui.viewport_width) * 0.5f;
                const float halfH = Field(player, ui.viewport_height) * 0.5f;
                // NDC y points up, screen y points down.
                Field(player, ui.viewport_left) = base.left + static_cast<int>(ndcX * halfW);
                Field(player, ui.viewport_top) = base.top - static_cast<int>(ndcY * halfH);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    LeaveCriticalSection(cs);
}

}  // namespace hud_prompt
