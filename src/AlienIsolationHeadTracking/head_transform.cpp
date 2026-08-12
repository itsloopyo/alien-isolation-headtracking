#include "head_transform.h"

#include <cmath>

#include "matrix_math.h"

namespace camera {

void WorldUpInCameraSpace(const float* V, float* upCam) {
    for (int r = 0; r < 3; ++r)
        upCam[r] =
            V[r * 4] * kWorldUp[0] + V[r * 4 + 1] * kWorldUp[1] + V[r * 4 + 2] * kWorldUp[2];
}

void BuildHeadRotation(float yawDeg, float pitchDeg, float rollDeg, const float* yawAxisCam,
                       float* R) {
    const float cy = cosf(yawDeg * mat::kDegToRad), sy = sinf(yawDeg * mat::kDegToRad);
    const float cp = cosf(pitchDeg * mat::kDegToRad), sp = sinf(pitchDeg * mat::kDegToRad);
    const float cr = cosf(-rollDeg * mat::kDegToRad), sr = sinf(-rollDeg * mat::kDegToRad);

    float u[3] = {yawAxisCam[0], yawAxisCam[1], yawAxisCam[2]};
    mat::Normalize3(u);

    // Rodrigues about u. The rotation is expressed in camera-local axes because
    // that is the frame every injection point applies it in (basis' = basis * R,
    // so the leftmost factor turns about the UNPITCHED camera axes).
    float ry[16], rx[16], rz[16], pitchRoll[16];
    mat::Identity(ry);
    const float ic = 1.0f - cy;
    ry[0] = cy + u[0] * u[0] * ic;
    ry[1] = u[0] * u[1] * ic - u[2] * sy;
    ry[2] = u[0] * u[2] * ic + u[1] * sy;
    ry[4] = u[1] * u[0] * ic + u[2] * sy;
    ry[5] = cy + u[1] * u[1] * ic;
    ry[6] = u[1] * u[2] * ic - u[0] * sy;
    ry[8] = u[2] * u[0] * ic - u[1] * sy;
    ry[9] = u[2] * u[1] * ic + u[0] * sy;
    ry[10] = cy + u[2] * u[2] * ic;

    mat::Identity(rx);
    rx[5] = cp;  rx[6] = -sp; rx[9] = sp;   rx[10] = cp;
    mat::Identity(rz);
    rz[0] = cr;  rz[1] = -sr; rz[4] = sr;   rz[5] = cr;

    mat::Mul4(rx, rz, pitchRoll);
    mat::Mul4(ry, pitchRoll, R);
}

bool HeadPose::IsNeutral() const {
    return fabsf(yaw) < 1e-4f && fabsf(pitch) < 1e-4f && fabsf(roll) < 1e-4f &&
           fabsf(offset[0]) < 1e-5f && fabsf(offset[1]) < 1e-5f && fabsf(offset[2]) < 1e-5f;
}

bool HeadTransform::Build(const HeadPose& pose, const float* yawAxisCam) {
    if (pose.IsNeutral()) return false;

    BuildHeadRotation(pose.yaw, pose.pitch, pose.roll, yawAxisCam, m_rotation);

    float A[16];
    mat::Identity(A);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) A[r * 4 + c] = m_rotation[c * 4 + r];

    const float ox = pose.offset[0], oy = pose.offset[1], oz = pose.offset[2];
    mat::Copy16(A, m_s);
    for (int r = 0; r < 3; ++r)
        m_s[r * 4 + 3] = -(A[r * 4 + 0] * ox + A[r * 4 + 1] * oy + A[r * 4 + 2] * oz);

    // S^-1 = T(offset) * A^T: 3x3 = A^T (= the camera rotation), translation = offset.
    mat::Copy16(m_rotation, m_sInverse);
    m_sInverse[3] = ox;
    m_sInverse[7] = oy;
    m_sInverse[11] = oz;

    m_offset[0] = ox;
    m_offset[1] = oy;
    m_offset[2] = oz;
    m_hasOffset = fabsf(ox) > 1e-5f || fabsf(oy) > 1e-5f || fabsf(oz) > 1e-5f;
    return true;
}

void HeadTransform::ConjugateForView(const float* V, float* X, float* Xinv, float* camPos,
                                     float* camPosMoved) const {
    float Vinv[16], tmp[16];
    mat::InvertRigid(V, Vinv);
    mat::Mul4(m_s, V, tmp);
    mat::Mul4(Vinv, tmp, X);
    mat::Mul4(m_sInverse, V, tmp);
    mat::Mul4(Vinv, tmp, Xinv);

    // V' = A*T(-off)*V puts the camera at c + R^T*off, c = -R^T*t being the original.
    for (int r = 0; r < 3; ++r) {
        camPos[r] = Vinv[r * 4 + 3];
        camPosMoved[r] =
            camPos[r] + V[r] * m_offset[0] + V[4 + r] * m_offset[1] + V[8 + r] * m_offset[2];
    }
}

bool HeadTransform::ProjectCleanAim(float fx, float fy, float& ndcX, float& ndcY) const {
    if (fx < 1e-3f || fy < 1e-3f) return false;
    // Old view-space forward (0,0,1) expressed in the rotated view = column 2 of
    // A, and A is the transpose of the camera rotation, so that is its row 2.
    const float dx = m_rotation[8], dy = m_rotation[9], dz = m_rotation[10];
    if (dz <= 1e-3f) return false;
    ndcX = fx * dx / dz;
    ndcY = fy * dy / dz;
    return true;
}

}  // namespace camera
