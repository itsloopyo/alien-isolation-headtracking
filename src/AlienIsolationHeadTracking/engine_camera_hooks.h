#pragma once

// The hooks that inject inside AI.exe itself, upstream of the GPU.
//
// Three of them cooperate:
//
//  - The camera publish (Mode::GameCamera). The game camera entity is the only
//    camera the engine's visibility work consults - everything published
//    downstream is derived from it - so rotating it across the publish is what
//    makes culling follow the head. The rotation then stands for the rest of
//    the frame, because the engine's entity update places camera-attached
//    geometry - the space suit helmet - and does it long after the publish has
//    returned. It is lifted around the gameplay tasks instead, so those still
//    read the camera the body is aiming with.
//  - The camera setup (Mode::CameraSetup). Rotates the view matrix handed to
//    the engine's matrix build, which is downstream of culling. It also
//    publishes the player camera in EVERY mode, because the render callback
//    holds the whole pipeline until a player camera exists and this is the only
//    place that learns of one.
//  - The derived camera cache. The publish derives a forward vector from the
//    ROTATED view and caches it, and that snapshot is what gameplay aims with.
//    Culling reads it too, so it cannot simply be left clean: it is rotated
//    while the cull set is built and clean inside the gameplay tasks.
//
// Every address these use is pinned to one AI.exe build, so all of them stay
// dormant on any other.

namespace camera {
namespace engine {

// Installs the camera publish, derived-cache, frame-task and camera-setup
// hooks. Falls back to Mode::ConstantBuffers when the running build is not the
// one the addresses were derived from.
void Install();

// Drops the "this view is already ours" cache. The camera-setup hook passes a
// view it recognises straight through rather than compounding the rotation, and
// a mode change makes that record meaningless.
void ClearRotatedViewCache();

}  // namespace engine
}  // namespace camera
