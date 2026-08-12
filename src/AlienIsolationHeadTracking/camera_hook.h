#pragma once

namespace cameraunlock {
class UdpReceiver;
}

namespace camera {

// Initialize MinHook, start the tracking session and install every injection
// point: the DX11 Present-hook overlay that drives it, the engine-side camera
// hooks, and the constant-buffer hook. The receiver must outlive the hooks.
void Install(cameraunlock::UdpReceiver& receiver);

// Sets the current head pose as centre.
void Recenter();

// Master on/off for view injection.
void SetEnabled(bool enabled);
bool IsEnabled();

// Cycles the injection point: the camera's look-at (upstream of the matrix
// build, and the only one culling can see), the camera setup, or the constant
// buffers. Returns a name for the newly selected mode.
const char* CycleInjectionMode();

// Widens the projection handed to the engine. If CATHODE culls against it, the
// frustum widens with it and stops cutting geometry off at the edges of a
// head-turned view - at the cost of the picture zooming out.
void SetFrustumWidening(bool enabled);
bool IsFrustumWidening();

// Advances the tracking mode: rotation and position -> rotation only ->
// position only -> rotation and position. Returns a name for the new mode.
const char* CycleTrackingMode();

// Switches head yaw between the world up axis (horizon-locked, the default) and
// the camera's own up axis. Returns true if world-space yaw is now on.
bool ToggleYawMode();
bool IsWorldSpaceYaw();

// Tear down all hooks.
void Shutdown();

}  // namespace camera
