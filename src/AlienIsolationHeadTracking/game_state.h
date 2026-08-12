#pragma once

// Whether the game is running or paused.
//
// Head tracking has to go quiet whenever the player is not in the world: a
// pause menu is drawn over a live 3D scene, so without this the view keeps
// swinging behind it. CATHODE keeps a mask of the reasons it considers itself
// paused, so this covers the pause menu and everything else that sets one
// without the mod having to recognise each menu.

namespace camera {
namespace game_state {

// False on a build whose addresses we do not know, which reads as "playing" so
// tracking behaves exactly as it did before this existed.
bool IsPaused();

}  // namespace game_state
}  // namespace camera
