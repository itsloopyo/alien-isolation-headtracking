#pragma once

// Rewriting the camera matrices on their way to the GPU (Mode::ConstantBuffers).
//
// CATHODE spreads the camera across several constant buffers, and the same
// matrices reappear in more than one of them, so nothing here keys off a buffer
// size or a fixed offset: every 16-byte offset of every buffer is classified
// from its own contents.
//
// Rotating the view-projection alone swings the view but leaves lighting and
// shadows swimming, because every other matrix still describes the un-rotated
// camera. So one head transform is derived per buffer and applied consistently
// to every camera matrix in it - post-multiplied into everything carrying the
// view, pre-multiplied into everything inverting it.
//
// The game never sees any of this: the transform exists only in the mapped GPU
// buffer, so aim, raycasts and AI perception read the clean camera. It is also
// the only injection point that needs no pinned address, and so the only one
// available on a build we have not fingerprinted.

namespace camera {
namespace constant_buffers {

// Creates the lock the Map/Unmap detours share. Must run before any hook that
// can lead to a mapped buffer being taken.
void Initialize();

// Installs the ID3D11DeviceContext Map/Unmap detours.
void Install();

// Clears the per-frame reference-camera lock. Called once per presented frame.
void BeginFrame();

// The backbuffer aspect, for the diagnostic that reports why no reference
// camera has been picked yet.
void SetRenderAspect(float aspect);

}  // namespace constant_buffers
}  // namespace camera
