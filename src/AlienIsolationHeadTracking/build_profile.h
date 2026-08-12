#pragma once

#include <windows.h>

#include <cstdint>

#include "cameraunlock/memory/pe_fingerprint.h"

// Every hard-coded address in this mod was derived from one AI.exe. On any other
// build those addresses point at unrelated data, so nothing that consumes one
// may run until the running executable has been identified - not the hooks, and
// not the UI-manager walk the interaction prompt does. A mismatch means fully
// dormant, so the game runs exactly vanilla rather than corrupting a patched
// executable.
//
// So every RVA and every game struct offset lives here, on a named profile,
// rather than loose beside the code that reads it. A future build is a new
// profile appended alongside this one, never an edit to these numbers: a user
// who has not taken the patch keeps matching the profile they are running.
namespace builds {

constexpr int kRenderTaskCount = 4;
constexpr int kCleanTaskCount = 8;

// Everything pinned to one shipped AI.exe.
struct OffsetTable {
    // Functions we detour, as RVAs from the module base.
    struct Hooks {
        // FUN_00432300 publishes the camera's eye, look-at target, up vector and
        // fov; the render matrices are built from them much later. That makes it
        // the furthest upstream point we can reach, and the only one with a
        // chance of the engine's visibility work seeing a head-rotated camera.
        uintptr_t camera_inputs;
        // FUN_0097a8a0 - the engine's camera setup, downstream of culling:
        //   FUN_0097a8a0(camera, renderer, view, proj, proj2)
        //     camera+0x280 = view    camera+0x200 = proj    camera+0x140 = proj2
        //     camera+0x000 = view * proj          <- the view-projection
        //     camera+0x040 = inverse(...)
        //     ... six more derived matrices ... then FUN_0097a100 uploads the lot
        uintptr_t camera_setup;
        // FUN_009b2fb0 derives, from a camera object's view matrix, both the
        // camera position (+0xC0) and a forward vector (+0xD0), and caches them
        // on the object. That forward is the snapshot gameplay aims with.
        uintptr_t camera_derive;
        // FUN_00d0fb70 - a stripped debug-flag lookup, hardwired to no.
        uintptr_t debug_flag;
        // Frame tasks that build the cull set, so they must run with the camera
        // rotated. Observation only.
        uintptr_t render_tasks[kRenderTaskCount];
        // Frame tasks that read the cached forward for gameplay, so they must
        // run with the clean one.
        uintptr_t clean_tasks[kCleanTaskCount];
    } hooks;

    // Fields of the camera entity FUN_00432300 reads the camera from. Selection
    // logic copied from the decompilation: a primary unless a secondary is
    // flagged live.
    struct CameraEntity {
        uintptr_t primary;
        uintptr_t primary_enabled;
        uintptr_t primary_suppressed;
        uintptr_t secondary;
        uintptr_t secondary_live;
        // Confirmed at runtime: a unit quaternion, and a position matching the
        // published eye exactly.
        uintptr_t orientation;
        uintptr_t position;
    } entity;

    // The camera object FUN_009b2fb0 caches its results on. The forward vector
    // is indexed as floats, which is how the hook writes it.
    struct RenderCamera {
        uintptr_t published_eye;
        int cached_forward_float;
    } render_camera;

    // The engine's own pause state. The PAUSE_MODE_PROCESS frame task is a thunk
    // that loads this singleton and calls its update, which keeps a mask of the
    // reasons the game is paused (0x2000 the pause menu, 0x10000 the challenge
    // map overlay, and more) and latches it into a second field it then acts on -
    // that latched field is what the engine itself treats as "we are paused",
    // and it is what makes it go quiet for menus this mod has never heard of.
    struct GameState {
        uintptr_t pause_manager_ptr;
        uintptr_t paused;
    } game_state;

    // The UI manager singleton and the Scaleform overlay records it holds. The
    // interaction prompt is repositioned by moving its player's GFx viewport
    // origin, which needs no VM call and no display-list access.
    struct Ui {
        uintptr_t manager_ptr;
        uintptr_t manager_lock;
        uintptr_t manager_overlay_array;
        uintptr_t manager_overlay_count;
        uintptr_t overlay_name;
        uintptr_t overlay_player;
        // GFx viewport on the Scaleform player: buffer size, then the on-screen
        // origin, then the drawn size.
        uintptr_t viewport_left;
        uintptr_t viewport_top;
        uintptr_t viewport_width;
        uintptr_t viewport_height;
    } ui;
};

struct BuildProfile {
    const char* name;
    DWORD time_date_stamp;
    DWORD size_of_image;
    DWORD check_sum;
    OffsetTable offsets;
};

// Steam, x86, TimeDateStamp 2015-02-17.
constexpr BuildProfile kSteamProfile_20150217 = {
    "steam-win32-20150217",
    0x54E3199D,
    0x01F81000,
    0x014D4B2F,
    {
        {
            0x32300,    // camera_inputs
            0x57A8A0,   // camera_setup
            0x5B2FB0,   // camera_derive
            0x90FB70,   // debug_flag
            {0x3752C0, 0x375300, 0x375340, 0x375380},
            {0x383430, 0x3834C0, 0x3B4170, 0x3834A0, 0x383500, 0x383640, 0x3CA0B0, 0x3CD750},
        },
        {0x1D8, 0x14D, 0x14F, 0x1F0, 0x221, 0x2C, 0x3C},
        {0x1358100, 0x34},
        {0x12F194C, 0x04},
        {0x134A78C, 0x04, 0x38, 0x3C, 0x34, 0x08, 0x58, 0x5C, 0x60, 0x64},
    },
};

inline const OffsetTable& ActiveOffsets() { return kSteamProfile_20150217.offsets; }

inline bool MatchesKnownBuild() {
    cameraunlock::memory::PeFingerprint fp{};
    if (!cameraunlock::memory::ReadPeFingerprint(GetModuleHandleW(nullptr), fp)) return false;
    const BuildProfile& profile = kSteamProfile_20150217;
    return fp.TimeDateStamp == profile.time_date_stamp && fp.SizeOfImage == profile.size_of_image &&
           fp.CheckSum == profile.check_sum;
}

}  // namespace builds
