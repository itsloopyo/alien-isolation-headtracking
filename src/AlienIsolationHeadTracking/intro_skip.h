#pragma once

namespace intro_skip {

// Turns the game's own frontend skip back on, so a test launch reaches the 3D
// scene without half a minute of splash screens. Dormant on an unrecognised
// build, like every other pinned-address hook.
void Install();

}  // namespace intro_skip
