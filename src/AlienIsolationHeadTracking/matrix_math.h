#pragma once

#include <cmath>

// Matrices are float[16], row-major (f[r*4+c] == M[r][c]).
//
// CATHODE stores row-major with the column-vector convention: the view matrix
// has its translation at f[3], f[7], f[11] and a (0,0,0,1) bottom row, and
// clip = M * v. Confirmed against a live buffer: P(+0x80) * V(+0x40) reproduces
// VP(+0xC0) exactly under this reading. View space is left-handed with +z
// forward and a 0.05 near plane (P's last two rows are (0,0,1,-0.05), (0,0,1,0),
// giving depth = 1 - near/z).
//
// Quaternions are float[4] laid out (x, y, z, w), matching the engine's own
// storage on the camera entity.
//
// Everything here is pure: no engine state, no logging, no side effects.

namespace mat {

constexpr float kDegToRad = 3.14159265358979f / 180.0f;

inline void Mul4(const float* A, const float* B, float* C) {  // C = A*B
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += A[r * 4 + k] * B[k * 4 + c];
            C[r * 4 + c] = s;
        }
}

inline void Copy16(const float* in, float* out) {
    for (int i = 0; i < 16; ++i) out[i] = in[i];
}

inline void PostMul(float* M, const float* X) {  // M = M*X
    float out[16];
    Mul4(M, X, out);
    Copy16(out, M);
}

inline void PreMul(float* M, const float* X) {  // M = X*M
    float out[16];
    Mul4(X, M, out);
    Copy16(out, M);
}

inline void Identity(float* M) {
    for (int i = 0; i < 16; ++i) M[i] = 0.0f;
    M[0] = M[5] = M[10] = M[15] = 1.0f;
}

inline void Transpose4(const float* in, float* out) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out[r * 4 + c] = in[c * 4 + r];
}

// Inverse of a rigid transform (orthonormal 3x3 + translation): [R^T | -R^T t].
inline void InvertRigid(const float* v, float* out) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) out[r * 4 + c] = v[c * 4 + r];
    out[12] = out[13] = out[14] = 0.0f;
    out[15] = 1.0f;
    const float tx = v[3], ty = v[7], tz = v[11];
    for (int r = 0; r < 3; ++r)
        out[r * 4 + 3] = -(out[r * 4 + 0] * tx + out[r * 4 + 1] * ty + out[r * 4 + 2] * tz);
}

inline float Mag3(float a, float b, float c) { return sqrtf(a * a + b * b + c * c); }

inline void Normalize3(float* v) {
    const float m = Mag3(v[0], v[1], v[2]);
    if (m < 1e-6f) return;
    v[0] /= m;
    v[1] /= m;
    v[2] /= m;
}

inline bool Finite(const float* f, int n) {
    for (int i = 0; i < n; ++i) {
        float v = f[i];
        if (!(v == v) || v > 1e18f || v < -1e18f) return false;
    }
    return true;
}

inline bool SameMatrix(const float* a, const float* b) {
    for (int i = 0; i < 16; ++i)
        if (fabsf(a[i] - b[i]) > 1e-5f) return false;
    return true;
}

inline bool IsIdentity(const float* f) {
    for (int i = 0; i < 16; ++i) {
        const float want = (i % 5 == 0) ? 1.0f : 0.0f;
        if (fabsf(f[i] - want) > 1e-4f) return false;
    }
    return true;
}

// --- quaternions ----------------------------------------------------------

inline float QuatLength(const float* q) {
    return sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
}

inline bool IsUnitQuat(const float* q) {
    const float len = QuatLength(q);
    return len > 0.99f && len < 1.01f;
}

inline void QuatConjugate(const float* q, float* out) {
    out[0] = -q[0];
    out[1] = -q[1];
    out[2] = -q[2];
    out[3] = q[3];
}

inline void QuatFromMatrix(const float* R, float* q) {
    const float trace = R[0] + R[5] + R[10];
    if (trace > 0.0f) {
        const float k = sqrtf(trace + 1.0f) * 2.0f;
        q[3] = 0.25f * k;
        q[0] = (R[9] - R[6]) / k;
        q[1] = (R[2] - R[8]) / k;
        q[2] = (R[4] - R[1]) / k;
    } else if (R[0] > R[5] && R[0] > R[10]) {
        const float k = sqrtf(1.0f + R[0] - R[5] - R[10]) * 2.0f;
        q[3] = (R[9] - R[6]) / k;
        q[0] = 0.25f * k;
        q[1] = (R[1] + R[4]) / k;
        q[2] = (R[2] + R[8]) / k;
    } else if (R[5] > R[10]) {
        const float k = sqrtf(1.0f + R[5] - R[0] - R[10]) * 2.0f;
        q[3] = (R[2] - R[8]) / k;
        q[0] = (R[1] + R[4]) / k;
        q[1] = 0.25f * k;
        q[2] = (R[6] + R[9]) / k;
    } else {
        const float k = sqrtf(1.0f + R[10] - R[0] - R[5]) * 2.0f;
        q[3] = (R[4] - R[1]) / k;
        q[0] = (R[2] + R[8]) / k;
        q[1] = (R[6] + R[9]) / k;
        q[2] = 0.25f * k;
    }
}

// a (x) b, both (x, y, z, w).
inline void QuatMul(const float* a, const float* b, float* out) {
    out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

// v rotated by quaternion q (x, y, z, w).
inline void QuatRotate(const float* q, const float* v, float* out) {
    const float t[3] = {2.0f * (q[1] * v[2] - q[2] * v[1]), 2.0f * (q[2] * v[0] - q[0] * v[2]),
                        2.0f * (q[0] * v[1] - q[1] * v[0])};
    out[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
    out[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
    out[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}

inline bool SameQuat(const float* a, const float* b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

inline bool SameVec3(const float* a, const float* b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

}  // namespace mat
