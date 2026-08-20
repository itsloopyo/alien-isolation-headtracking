#pragma once

namespace hud_prompt {

// Drives the game's screen-centred HUD to the head-tracked aim point, so the
// "E USE" prompt, its cursor dot and the weapon reticles sit under our own
// reticle instead of at screen centre. ndc is the aim point in normalised
// device coordinates (x right, y up); valid is false when the aim point is
// off-screen or behind the camera.
void Update(float ndcX, float ndcY, bool valid);

}  // namespace hud_prompt
