#pragma once

namespace camera_scan {

// Locates the engine's live camera transform in process memory. Results go to
// the log; nothing is written to game memory.
//
// Call once per frame. It arms hardware write breakpoints on the render camera
// block and reports every instruction that stores to it, then disarms itself
// after a few seconds - each trapped write costs an exception, so it cannot be
// left running.
void Update();

// Traps READS of an address and reports what consulted it, alongside whether
// the head rotation was applied at that moment. A gameplay reader that only
// ever sees the clean camera is not the leak; one that sees it rotated is.
void WatchReads(void* address, const char* label);

// Hunts memory for a float3 holding the given vector, narrowing as it changes.
// Used to find where a direction computed during the camera publish is cached:
// gameplay reads that copy, which is why reverting published state never
// decoupled aim.
void ScanForVector(const float* v);
void SetRotationStateProbe(bool (*isRotated)());

}  // namespace camera_scan
