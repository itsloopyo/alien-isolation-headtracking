#pragma once

namespace intro_skip {

// Turns the game's own frontend skip back on, so a launch reaches the 3D scene
// without half a minute of splash screens. Opt-in through SkipIntroMovies in the
// INI, because those splash screens carry the game's credits. Dormant when the
// setting is off, and on an unrecognised build like every other pinned-address
// hook.
void Install();

}  // namespace intro_skip
