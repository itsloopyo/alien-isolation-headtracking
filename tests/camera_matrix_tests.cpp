// Telling the player camera's matrices apart from everything else CATHODE
// uploads. These predicates decide what gets rewritten on its way to the GPU, so
// a false positive rotates a shadow map and a false negative leaves lighting
// swimming - both of which used to be findable only by playing the game.
//
// The fixtures build a camera the way the engine does, then feed the classifier
// exactly what the engine would publish.

#include "camera_matrix.h"

#include "matrix_math.h"
#include "test_support.h"

namespace {

using namespace camera;
using tests::Check;
using tests::CheckNear;
using tests::CheckNear16;

constexpr float kFx = 1.2f;
constexpr float kFy = 2.13f;  // fy/fx is the aspect, and must not read as square
constexpr float kNear = 0.05f;

// A rigid view matrix: yaw about world Y, then translated.
void MakeView(float yawRad, float tx, float ty, float tz, float* V) {
    const float c = cosf(yawRad), s = sinf(yawRad);
    mat::Identity(V);
    V[0] = c;   V[2] = s;
    V[8] = -s;  V[10] = c;
    V[3] = tx;
    V[7] = ty;
    V[11] = tz;
}

// CATHODE's projection: depth = 1 - near/z, so the last two rows are
// (0,0,1,-near) and (0,0,1,0).
void MakeProjection(float* P) {
    for (int i = 0; i < 16; ++i) P[i] = 0.0f;
    P[0] = kFx;
    P[5] = kFy;
    P[10] = 1.0f;
    P[11] = -kNear;
    P[14] = 1.0f;
}

// P^-1, from solving P*(x,y,z,w): rows (1/fx,0,0,0) (0,1/fy,0,0) (0,0,0,1)
// (0,0,-1/near,1/near).
void MakeInverseProjection(float* Pinv) {
    for (int i = 0; i < 16; ++i) Pinv[i] = 0.0f;
    Pinv[0] = 1.0f / kFx;
    Pinv[5] = 1.0f / kFy;
    Pinv[11] = 1.0f;
    Pinv[14] = -1.0f / kNear;
    Pinv[15] = 1.0f / kNear;
}

// A square projection - a shadow map or a cubemap face.
void MakeSquareProjection(float* P) {
    MakeProjection(P);
    P[5] = kFx;
}

void TestRigidViewAcceptsAViewAndRejectsAProjection() {
    float V[16], P[16], VP[16];
    MakeView(0.6f, 3.0f, 1.0f, -2.0f, V);
    MakeProjection(P);
    mat::Mul4(P, V, VP);

    Check(IsRigidView(V), "a view matrix is rigid");
    Check(!IsRigidView(VP), "a view-projection is not rigid");

    float scaled[16];
    mat::Copy16(V, scaled);
    scaled[0] *= 1.5f;  // breaks orthonormality
    Check(!IsRigidView(scaled), "a scaled basis row is not rigid");
}

void TestViewProjectionIsDistinguishedFromABareProjection() {
    float V[16], P[16], VP[16];
    MakeView(-0.9f, 10.0f, 2.0f, 4.0f, V);
    MakeProjection(P);
    mat::Mul4(P, V, VP);

    Check(IsViewProjection(VP), "P*V reads as a view-projection");
    Check(IsBareProjection(P), "a standalone P reads as a bare projection");
    Check(!IsViewProjection(P), "a standalone P is not treated as a view-projection");
    Check(!IsViewProjection(V), "a view matrix is not a view-projection");
}

void TestRecoverViewInvertsTheProjectionExactly() {
    float V[16], P[16], VP[16], recovered[16];
    MakeView(1.4f, -6.0f, 0.5f, 9.0f, V);
    MakeProjection(P);
    mat::Mul4(P, V, VP);

    float aspect = 0.0f;
    Check(RecoverView(VP, recovered, aspect), "the view is recoverable from P*V");
    CheckNear16(recovered, V, 1e-4f, "the recovered view matches the original");
    CheckNear(aspect, kFy / kFx, 1e-4f, "the recovered aspect is fy/fx");
}

void TestRecoverViewFromInverseInvertsTheInverseExactly() {
    float V[16], Vinv[16], Pinv[16], invVP[16], recovered[16];
    MakeView(0.25f, 2.0f, -1.0f, 5.0f, V);
    mat::InvertRigid(V, Vinv);
    MakeInverseProjection(Pinv);
    mat::Mul4(Vinv, Pinv, invVP);

    Check(RecoverViewFromInverse(invVP, recovered), "the view is recoverable from inv(P*V)");
    CheckNear16(recovered, V, 1e-3f, "the view recovered from the inverse matches the original");
}

void TestInverseViewProjectionIsIdentified() {
    float V[16], Vinv[16], Pinv[16], invVP[16], P[16], VP[16];
    MakeView(-0.4f, 1.0f, 2.0f, 3.0f, V);
    mat::InvertRigid(V, Vinv);
    MakeInverseProjection(Pinv);
    mat::Mul4(Vinv, Pinv, invVP);
    MakeProjection(P);
    mat::Mul4(P, V, VP);

    Check(LooksLikeInverseOfCamera(invVP, V), "inv(P*V) is recognised against its own camera");
    // The discriminator that matters: a forward projection can never satisfy the
    // (0, 0, -k, k) w row, even when the camera happens to be axis-aligned.
    Check(!LooksLikeInverseOfCamera(VP, V), "a forward view-projection is not an inverse");

    float otherView[16];
    MakeView(2.6f, 40.0f, 0.0f, 0.0f, otherView);
    Check(!LooksLikeInverseOfCamera(invVP, otherView),
          "an inverse belonging to another camera is rejected");
}

void TestCameraWorldMatrixIsIdentified() {
    float V[16], Vinv[16];
    MakeView(0.8f, -3.0f, 4.0f, 1.0f, V);
    mat::InvertRigid(V, Vinv);

    Check(LooksLikeCameraWorld(Vinv, V), "V^-1 is recognised as this camera's world matrix");
    Check(!LooksLikeCameraWorld(V, V), "the view itself is not its own camera-world matrix");

    float otherView[16];
    MakeView(-1.9f, 0.0f, 0.0f, 0.0f, otherView);
    Check(!LooksLikeCameraWorld(Vinv, otherView),
          "another camera's world matrix is rejected");
}

void TestSameCameraComparesOrientationAndPosition() {
    float a[16], b[16];
    MakeView(0.5f, 1.0f, 2.0f, 3.0f, a);

    mat::Copy16(a, b);
    Check(SameCamera(a, b), "a camera matches itself");

    // A frame of movement is still the same camera.
    MakeView(0.5f, 1.1f, 2.0f, 3.0f, b);
    Check(SameCamera(a, b), "a small translation is still the same camera");

    // A reflection probe sitting somewhere else is not.
    MakeView(0.5f, 20.0f, 2.0f, 3.0f, b);
    Check(!SameCamera(a, b), "a camera metres away is a different camera");

    MakeView(1.9f, 1.0f, 2.0f, 3.0f, b);
    Check(!SameCamera(a, b), "a camera pointing elsewhere is a different camera");
}

// A torch is a spotlight at the camera position pointing down the camera
// forward, so its shadow view matrix is identical to the player's. Its aspect is
// not, and that is the only thing keeping it out of the rewritten set.
void TestSquareProjectionsAreRejectedByAspect() {
    float V[16], square[16], squareVP[16], recovered[16];
    MakeView(0.3f, 0.0f, 0.0f, 0.0f, V);
    MakeSquareProjection(square);
    mat::Mul4(square, V, squareVP);

    float aspect = 0.0f;
    Check(RecoverView(squareVP, recovered, aspect), "a square projection still recovers a view");
    Check(!PlausibleCameraAspect(aspect), "a square aspect is rejected");
    Check(PlausibleCameraAspect(kFy / kFx), "a non-square aspect is accepted");
}

}  // namespace

int RunCameraMatrixTests() {
    tests::Begin("camera_matrix");
    TestRigidViewAcceptsAViewAndRejectsAProjection();
    TestViewProjectionIsDistinguishedFromABareProjection();
    TestRecoverViewInvertsTheProjectionExactly();
    TestRecoverViewFromInverseInvertsTheInverseExactly();
    TestInverseViewProjectionIsIdentified();
    TestCameraWorldMatrixIsIdentified();
    TestSameCameraComparesOrientationAndPosition();
    TestSquareProjectionsAreRejectedByAspect();
    return tests::g_failures;
}
