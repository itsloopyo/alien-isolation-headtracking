// The matrix and quaternion primitives every injection point is built from.
// They ran only inside a hooked game process before; a sign error in any of them
// showed up as "the view is wrong somehow" with no way to bisect it.

#include "matrix_math.h"

#include <limits>

#include "test_support.h"

namespace {

using tests::Check;
using tests::CheckNear;
using tests::CheckNear16;

// A rigid view matrix: yaw about world Y, translated. Row-major, translation in
// the last column, exactly as CATHODE stores it.
void MakeView(float yawRad, float tx, float ty, float tz, float* V) {
    const float c = cosf(yawRad), s = sinf(yawRad);
    mat::Identity(V);
    V[0] = c;   V[2] = s;
    V[8] = -s;  V[10] = c;
    V[3] = tx;
    V[7] = ty;
    V[11] = tz;
}

void TestIdentityIsMultiplicativeUnit() {
    float I[16], V[16], out[16];
    mat::Identity(I);
    MakeView(0.7f, 1.0f, -2.0f, 3.0f, V);

    mat::Mul4(I, V, out);
    CheckNear16(out, V, 1e-6f, "I*V == V");
    mat::Mul4(V, I, out);
    CheckNear16(out, V, 1e-6f, "V*I == V");
    Check(mat::IsIdentity(I), "IsIdentity accepts the identity");
    Check(!mat::IsIdentity(V), "IsIdentity rejects a rotation");
}

void TestPostAndPreMulMatchMul4() {
    float A[16], B[16], expected[16], actual[16];
    MakeView(0.3f, 1.0f, 2.0f, 3.0f, A);
    MakeView(-1.1f, -4.0f, 0.5f, 2.0f, B);

    mat::Mul4(A, B, expected);
    mat::Copy16(A, actual);
    mat::PostMul(actual, B);
    CheckNear16(actual, expected, 1e-6f, "PostMul(M, X) == M*X");

    mat::Mul4(B, A, expected);
    mat::Copy16(A, actual);
    mat::PreMul(actual, B);
    CheckNear16(actual, expected, 1e-6f, "PreMul(M, X) == X*M");
}

void TestInvertRigidRoundTrips() {
    float V[16], Vinv[16], product[16], I[16];
    mat::Identity(I);
    MakeView(1.2f, 12.0f, -3.0f, 7.5f, V);
    mat::InvertRigid(V, Vinv);

    mat::Mul4(V, Vinv, product);
    CheckNear16(product, I, 1e-5f, "V * V^-1 == I");
    mat::Mul4(Vinv, V, product);
    CheckNear16(product, I, 1e-5f, "V^-1 * V == I");

    // The inverse's translation is the camera's world position.
    CheckNear(Vinv[3], -(V[0] * V[3] + V[4] * V[7] + V[8] * V[11]), 1e-5f,
              "V^-1 translation is -R^T t");
}

void TestTransposeIsItsOwnInverse() {
    float V[16], t[16], tt[16];
    MakeView(0.9f, 1.0f, 2.0f, 3.0f, V);
    mat::Transpose4(V, t);
    mat::Transpose4(t, tt);
    CheckNear16(tt, V, 1e-6f, "transposing twice returns the original");
    CheckNear(t[12], V[3], 1e-6f, "transpose moves the translation to the last row");
}

void TestFiniteRejectsGarbage() {
    float ok[4] = {0.0f, 1.0f, -1.0f, 1e17f};
    Check(mat::Finite(ok, 4), "finite values pass");

    const float huge[4] = {0.0f, 0.0f, 0.0f, 1e19f};
    Check(!mat::Finite(huge, 4), "an out-of-range magnitude fails");

    const float nan[4] = {0.0f, 0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN()};
    Check(!mat::Finite(nan, 4), "a NaN fails");
}

void TestSameMatrixIsExactToATolerance() {
    float a[16], b[16];
    MakeView(0.4f, 1.0f, 2.0f, 3.0f, a);
    mat::Copy16(a, b);
    Check(mat::SameMatrix(a, b), "a copy compares equal");
    b[7] += 1e-3f;
    Check(!mat::SameMatrix(a, b), "a millimetre of translation compares unequal");
}

// The entity path converts the head rotation to a quaternion and back through
// the engine's own orientation, so the two representations have to agree.
void TestQuatFromMatrixMatchesTheMatrix() {
    float R[16], q[4];
    MakeView(0.85f, 0.0f, 0.0f, 0.0f, R);
    mat::QuatFromMatrix(R, q);
    Check(mat::IsUnitQuat(q), "a rotation converts to a unit quaternion");

    const float v[3] = {0.3f, -0.7f, 0.64f};
    float byQuat[3];
    mat::QuatRotate(q, v, byQuat);

    const float byMatrix[3] = {R[0] * v[0] + R[1] * v[1] + R[2] * v[2],
                               R[4] * v[0] + R[5] * v[1] + R[6] * v[2],
                               R[8] * v[0] + R[9] * v[1] + R[10] * v[2]};
    CheckNear(byQuat[0], byMatrix[0], 1e-5f, "quaternion rotation matches the matrix (x)");
    CheckNear(byQuat[1], byMatrix[1], 1e-5f, "quaternion rotation matches the matrix (y)");
    CheckNear(byQuat[2], byMatrix[2], 1e-5f, "quaternion rotation matches the matrix (z)");
}

// RevertEntityToClean undoes the head rotation by multiplying in its conjugate.
void TestConjugateUndoesARotation() {
    float R[16], head[4], clean[4] = {0.1f, 0.2f, 0.3f, 0.927f};
    MakeView(0.5f, 0.0f, 0.0f, 0.0f, R);
    mat::QuatFromMatrix(R, head);

    float rotated[4], inverse[4], recovered[4];
    mat::QuatMul(clean, head, rotated);
    mat::QuatConjugate(head, inverse);
    mat::QuatMul(rotated, inverse, recovered);

    for (int i = 0; i < 4; ++i)
        CheckNear(recovered[i], clean[i], 1e-5f, "conjugate multiplication restores the original");
}

void TestSameQuatIsAnExactComparison() {
    const float a[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    float b[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    Check(mat::SameQuat(a, b), "an identical quaternion compares equal");
    // The entity path uses this to ask "has the game overwritten what we wrote",
    // so the comparison has to be exact rather than approximate.
    b[2] += 1e-7f;
    Check(!mat::SameQuat(a, b), "any difference at all compares unequal");
}

}  // namespace

int RunMatrixMathTests() {
    tests::Begin("matrix_math");
    TestIdentityIsMultiplicativeUnit();
    TestPostAndPreMulMatchMul4();
    TestInvertRigidRoundTrips();
    TestTransposeIsItsOwnInverse();
    TestFiniteRejectsGarbage();
    TestSameMatrixIsExactToATolerance();
    TestQuatFromMatrixMatchesTheMatrix();
    TestConjugateUndoesARotation();
    TestSameQuatIsAnExactComparison();
    return tests::g_failures;
}
