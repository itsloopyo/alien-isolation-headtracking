#pragma once

// Reads the scene depth buffer back, one texel at a time.
//
// This is where the reticle's distance to its target comes from (aim_point.h
// explains why it needs one). The depth buffer already holds that distance at
// the exact pixel the reticle is drawn on, so nothing has to be traced, no
// physics world has to be found, and no address has to be pinned - which is
// what makes it survive a game patch.
//
// The readback is asynchronous by design. The copy is issued one presented
// frame and collected a frame or two later, because a blocking Map on a
// just-written depth buffer stalls the render thread on the GPU. The value that
// comes back is therefore a couple of frames old, which the thing it is used for
// can afford: the target the player is looking at moves slowly next to a frame,
// and the distance is smoothed on top of that anyway.

namespace camera {
namespace depth_probe {

// Hooks OMSetRenderTargets, which is how the frame's scene depth buffer is
// identified. Requires MinHook to be initialised.
void Install();

// Collects the readback in flight and starts the next one. Called once per
// presented frame, with the size of the frame being presented - that size is
// also what the scene depth buffer is picked by, so a shadow map or a cube face
// can never be mistaken for it.
//
// `wantSample` is false when there is no aim point to read under, in which case
// the readback still gets drained but nothing is decoded. Returns true only when
// a depth value was actually read, and writes it to `depth` (0 at the near
// plane, 1 at infinity).
bool Update(int frameWidth, int frameHeight, bool wantSample, float ndcX, float ndcY,
            float& depth);

void Shutdown();

}  // namespace depth_probe
}  // namespace camera
