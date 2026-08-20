#include "aim_point.h"

#include <cmath>

namespace camera {
namespace {

bool IsOne(float v) { return fabsf(v - 1.0f) < 1e-3f; }

// A near plane outside this is not a near plane - the engine's is 0.05, and
// anything a hundred times either side of that says the matrix was read wrong.
bool PlausibleNearPlane(float n) { return n > 1e-4f && n < 5.0f; }

}  // namespace

bool NearPlaneFromProjection(const float* proj, float& nearZ) {
    if (!proj) return false;
    // Shared by both storage orders: the depth row's own z term, and the zero
    // that makes the far plane infinite.
    if (!IsOne(proj[10]) || fabsf(proj[15]) > 1e-3f) return false;

    if (IsOne(proj[14]) && proj[11] < 0.0f)
        nearZ = -proj[11];  // row-major, column-vector: the constant buffers
    else if (IsOne(proj[11]) && proj[14] < 0.0f)
        nearZ = -proj[14];  // transposed: the camera setup
    else
        return false;

    return PlausibleNearPlane(nearZ);
}

bool NearPlaneFromViewProjection(const float* vp, float& nearZ) {
    if (!vp) return false;
    nearZ = vp[15] - vp[11];
    return PlausibleNearPlane(nearZ);
}

bool AimDistanceFromDepth(const AimFrame& frame, float depth, float& outDistance) {
    if (!frame.valid || frame.fx < 1e-3f || frame.fy < 1e-3f) return false;
    if (!PlausibleNearPlane(frame.nearZ)) return false;
    if (!(depth > 0.0f) || depth >= kSkyDepth) return false;

    const float z = frame.nearZ / (1.0f - depth);
    if (!std::isfinite(z) || !(z > 0.0f)) return false;

    // The surface under the reticle, in the coordinates the frame was rendered
    // in: the perspective divide the projection applied, run backwards.
    const float sx = frame.ndcX * z / frame.fx;
    const float sy = frame.ndcY * z / frame.fy;

    const float t = (sx - frame.eye[0]) * frame.dir[0] + (sy - frame.eye[1]) * frame.dir[1] +
                    (z - frame.eye[2]) * frame.dir[2];
    if (!std::isfinite(t) || !(t > 0.0f)) return false;

    outDistance = t;
    return true;
}

}  // namespace camera
