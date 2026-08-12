#include "camera_matrix.h"

#include <cmath>

#include "matrix_math.h"

namespace camera {
namespace {

using mat::Finite;
using mat::Mag3;

bool NearAxisW(float a, float b, float c, float d) {  // ~ (0,0,0,1)
    return fabsf(a) < 0.001f && fabsf(b) < 0.001f && fabsf(c) < 0.001f && fabsf(d - 1.0f) < 0.001f;
}

bool LooksLikeProjective(const float* f) {
    if (!Finite(f, 16)) return false;
    if (NearAxisW(f[12], f[13], f[14], f[15])) return false;
    if (NearAxisW(f[3], f[7], f[11], f[15])) return false;
    float rm[4] = {Mag3(f[0], f[1], f[2]), Mag3(f[4], f[5], f[6]), Mag3(f[8], f[9], f[10]),
                   Mag3(f[12], f[13], f[14])};
    int unit = 0;
    for (int i = 0; i < 4; ++i)
        if (rm[i] > 0.9f && rm[i] < 1.1f) ++unit;
    return unit >= 2;
}

}  // namespace

bool PlausibleCameraAspect(float aspect) { return fabsf(aspect - 1.0f) > 0.05f; }

bool IsBareProjection(const float* f) {
    return fabsf(f[12]) < 1e-4f && fabsf(f[13]) < 1e-4f && fabsf(fabsf(f[14]) - 1.0f) < 1e-3f &&
           fabsf(f[15]) < 1e-4f;
}

bool IsViewProjection(const float* f) {
    return LooksLikeProjective(f) && !IsBareProjection(f);
}

bool IsRigidView(const float* f) {
    if (!Finite(f, 16)) return false;
    if (!NearAxisW(f[12], f[13], f[14], f[15])) return false;
    for (int r = 0; r < 3; ++r) {
        const float m = Mag3(f[r * 4], f[r * 4 + 1], f[r * 4 + 2]);
        if (m < 0.99f || m > 1.01f) return false;
    }
    for (int a = 0; a < 3; ++a) {
        const float* ra = f + a * 4;
        const float* rb = f + ((a + 1) % 3) * 4;
        const float d = ra[0] * rb[0] + ra[1] * rb[1] + ra[2] * rb[2];
        if (fabsf(d) > 0.01f) return false;
    }
    return true;
}

bool RecoverView(const float* vp, float* V, float& aspect) {
    const float fx = Mag3(vp[0], vp[1], vp[2]);
    const float fy = Mag3(vp[4], vp[5], vp[6]);
    if (fx < 1e-3f || fy < 1e-3f) return false;
    aspect = fy / fx;
    if (fabsf(Mag3(vp[12], vp[13], vp[14]) - 1.0f) > 0.01f) return false;
    for (int c = 0; c < 3; ++c) {
        V[c] = vp[c] / fx;
        V[4 + c] = vp[4 + c] / fy;
        V[8 + c] = vp[12 + c];
    }
    V[3] = vp[3] / fx;
    V[7] = vp[7] / fy;
    V[11] = vp[15];
    V[12] = V[13] = V[14] = 0.0f;
    V[15] = 1.0f;
    return IsRigidView(V);
}

bool RecoverViewFromInverse(const float* m, float* V) {
    const float k = m[15];
    if (k <= 1e-4f) return false;
    const float m0 = Mag3(m[0], m[4], m[8]);
    const float m1 = Mag3(m[1], m[5], m[9]);
    if (m0 < 1e-6f || m1 < 1e-6f) return false;

    const float right[3] = {m[0] / m0, m[4] / m0, m[8] / m0};
    const float up[3] = {m[1] / m1, m[5] / m1, m[9] / m1};
    const float fwd[3] = {right[1] * up[2] - right[2] * up[1], right[2] * up[0] - right[0] * up[2],
                          right[0] * up[1] - right[1] * up[0]};
    const float c[3] = {-m[2] / k, -m[6] / k, -m[10] / k};

    for (int i = 0; i < 3; ++i) {
        V[i] = right[i];
        V[4 + i] = up[i];
        V[8 + i] = fwd[i];
    }
    V[3] = -(right[0] * c[0] + right[1] * c[1] + right[2] * c[2]);
    V[7] = -(up[0] * c[0] + up[1] * c[1] + up[2] * c[2]);
    V[11] = -(fwd[0] * c[0] + fwd[1] * c[1] + fwd[2] * c[2]);
    V[12] = V[13] = V[14] = 0.0f;
    V[15] = 1.0f;
    return IsRigidView(V);
}

bool SameCamera(const float* a, const float* b) {
    for (int r = 0; r < 3; ++r) {
        const float d =
            a[r * 4] * b[r * 4] + a[r * 4 + 1] * b[r * 4 + 1] + a[r * 4 + 2] * b[r * 4 + 2];
        if (d < 0.9995f) return false;
    }
    return Mag3(a[3] - b[3], a[7] - b[7], a[11] - b[11]) < 0.5f;
}

bool LooksLikeInverseOfCamera(const float* m, const float* V) {
    const float k = m[15];
    if (k <= 1e-4f) return false;
    if (fabsf(m[14] + k) > 1e-3f * k) return false;
    const float scale = k + 1.0f;
    if (fabsf(m[12]) > 1e-3f * scale || fabsf(m[13]) > 1e-3f * scale) return false;
    const float m0 = Mag3(m[0], m[4], m[8]);
    const float m1 = Mag3(m[1], m[5], m[9]);
    if (m0 < 1e-4f || m1 < 1e-4f) return false;
    // Those magnitudes are 1/fx and 1/fy, so their ratio is the aspect - the
    // same test that keeps a torch's shadow projection out of the forward set.
    if (!PlausibleCameraAspect(m0 / m1)) return false;
    const float d0 = (m[0] * V[0] + m[4] * V[1] + m[8] * V[2]) / m0;
    const float d1 = (m[1] * V[4] + m[5] * V[5] + m[9] * V[6]) / m1;
    return d0 > 0.9995f && d1 > 0.9995f;
}

bool LooksLikeCameraWorld(const float* m, const float* V) {
    if (!IsRigidView(m)) return false;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            if (fabsf(m[r * 4 + c] - V[c * 4 + r]) > 0.002f) return false;
    const float t0 = V[3], t1 = V[7], t2 = V[11];
    for (int r = 0; r < 3; ++r) {
        const float c = -(V[r] * t0 + V[4 + r] * t1 + V[8 + r] * t2);
        if (fabsf(m[r * 4 + 3] - c) > 0.01f) return false;
    }
    return true;
}

}  // namespace camera
