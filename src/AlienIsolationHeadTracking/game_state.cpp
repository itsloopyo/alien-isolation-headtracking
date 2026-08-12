#include "game_state.h"

#include <windows.h>

#include <cstdint>

#include "build_profile.h"

#include "cameraunlock/logging/file_log.h"

namespace camera {
namespace game_state {
namespace {

using namespace cameraunlock;

// The singleton is null until the game has built its frame graph, and the read
// runs every frame, so a fault here would be a fault every frame. Guarded rather
// than trusted: this pointer belongs to the engine, not to us.
bool ReadPausedMask(uint32_t& mask) {
    const builds::OffsetTable::GameState& g = builds::ActiveOffsets().game_state;
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try {
        const uintptr_t manager = *reinterpret_cast<const uintptr_t*>(base + g.pause_manager_ptr);
        if (!manager) return false;
        mask = *reinterpret_cast<const uint32_t*>(manager + g.paused);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void ReportChange(uint32_t mask) {
    static bool have = false;
    static uint32_t last = 0;
    if (have && mask == last) return;
    have = true;
    last = mask;
    if (mask)
        logging::Line("game: paused (0x%X), tracking suppressed", mask);
    else
        logging::Line("game: playing, tracking live");
}

}  // namespace

bool IsPaused() {
    // Fingerprinting walks the PE headers, and this runs every frame.
    static const bool known = builds::MatchesKnownBuild();
    if (!known) return false;
    uint32_t mask = 0;
    if (!ReadPausedMask(mask)) return false;
    ReportChange(mask);
    return mask != 0;
}

}  // namespace game_state
}  // namespace camera
