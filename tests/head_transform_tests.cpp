// The head pose maths every injection point applies.
//
// The reticle litmus tests from the catalogue doctrine live here: the projection
// of the clean aim point must be derived from the exact rotation the camera is
// given, or the marker and the shot drift apart on combined poses. That is
// checkable without a game, and now is.

#include "head_transform.h"

#include "matrix_math.h"
#include "test_support.h"

namespace {

using namespace camera;
using tests::Check;
using tests::CheckNear;
using tests::CheckNear16;

constexpr float kFx = 1.2f;
constexpr float kFy = 2.13f;

// The explicit camera-local yaw matrix the mod carried before the two yaw modes
// were unified onto one Rodrigues build. Horizon-locked yaw about the camera's
// own up axis has to reproduce it exactly, which is what makes the two modes
// coincide with the camera level.
void MakeLocalYawPitchRoll(float yawDeg, float pitchDeg, float rollDeg, float* R) {
    const float k = mat::kDegToRad;
    const float cy = cosf(yawDeg * k), sy = sinf(yawDeg * k);
    const float cp = cosf(pitchDeg * k), sp = sinf(pitchDeg * k);
    const float cr = cosf(-rollDeg * k), sr = sinf(-rollDeg * k);

    float ry[16], rx[16], rz[16], tmp[16];
    mat::Identity(ry);
    ry[0] = cy;  ry[2] = sy;  ry[8] = -sy;  ry[10] = cy;
    mat::Identity(rx);
    rx[5] = cp;  rx[6] = -sp; rx[9] = sp;   rx[10] = cp;
    mat::Identity(rz);
    rz[0] = cr;  rz[1] = -sr; rz[4] = sr;   rz[5] = cr;

    mat::Mul4(ry, rx, tmp);
    mat::Mul4(tmp, rz, R);
}

void MakeView(float yawRad, float tx, float ty, float tz, float* V) {
    const float c = cosf(yawRad), s = sinf(yawRad);
    mat::Identity(V);
    V[0] = c;   V[2] = s;
    V[8] = -s;  V[10] = c;
    V[3] = tx;
    V[7] = ty;
    V[11] = tz;
}

HeadPose MakePose(float yaw, float pitch, float roll) {
    HeadPose pose;
    pose.yaw = yaw;
    pose.pitch = pitch;
    pose.roll = roll;
    return pose;
}

void TestNeutralPosesAreNotInjected() {
    Check(HeadPose().IsNeutral(), "a default pose is neutral");
    Check(MakePose(0.00005f, -0.00005f, 0.0f).IsNeutral(), "a pose below the angle floor is neutral");
    Check(!MakePose(0.5f, 0.0f, 0.0f).IsNeutral(), "half a degree of yaw is not neutral");

    HeadPose leaning;
    leaning.offset[0] = 0.02f;
    Check(!leaning.IsNeutral(), "two centimetres of lean is not neutral");

    HeadTransform transform;
    Check(!transform.Build(HeadPose(), kCameraUp), "building a neutral pose reports nothing to do");
}

// Camera-local yaw is the Rodrigues build about (0,1,0), so it must agree with
// the explicit yaw matrix it replaced on every axis and every combination.
void TestCameraLocalYawMatchesTheExplicitBuild() {
    const float poses[][3] = {{25.0f, 0.0f, 0.0f},   {0.0f, -18.0f, 0.0f}, {0.0f, 0.0f, 12.0f},
                              {30.0f, -20.0f, 8.0f}, {-45.0f, 35.0f, -15.0f}};
    for (const float* p : poses) {
        float expected[16], actual[16];
        MakeLocalYawPitchRoll(p[0], p[1], p[2], expected);
        BuildHeadRotation(p[0], p[1], p[2], kCameraUp, actual);
        CheckNear16(actual, expected, 1e-5f, "Rodrigues yaw about camera up matches the explicit build");
    }
}

// With the camera level, the world up expressed in camera coordinates IS the
// camera's own up, so the two yaw modes have to coincide.
void TestWorldYawReducesToLocalYawWithTheCameraLevel() {
    float level[16], upCam[3], world[16], local[16];
    MakeView(0.7f, 0.0f, 0.0f, 0.0f, level);  // yaw only: still level
    WorldUpInCameraSpace(level, upCam);
    CheckNear(upCam[0], 0.0f, 1e-5f, "a level camera sees world up on its own up axis (x)");
    CheckNear(upCam[1], 1.0f, 1e-5f, "a level camera sees world up on its own up axis (y)");
    CheckNear(upCam[2], 0.0f, 1e-5f, "a level camera sees world up on its own up axis (z)");

    BuildHeadRotation(20.0f, -10.0f, 5.0f, upCam, world);
    BuildHeadRotation(20.0f, -10.0f, 5.0f, kCameraUp, local);
    CheckNear16(world, local, 1e-5f, "world-space yaw reduces to camera-local yaw when level");
}

void TestRotationStaysOrthonormal() {
    float R[16];
    BuildHeadRotation(37.0f, -22.0f, 11.0f, kCameraUp, R);
    for (int r = 0; r < 3; ++r)
        CheckNear(mat::Mag3(R[r * 4], R[r * 4 + 1], R[r * 4 + 2]), 1.0f, 1e-5f,
                  "each rotation row is unit length");
    CheckNear(R[0] * R[4] + R[1] * R[5] + R[2] * R[6], 0.0f, 1e-5f, "rotation rows stay orthogonal");
}

// Litmus 1: pure roll leaves the aim point at screen centre.
// Litmus 2: pure pitch moves it vertically only.
// Litmus 3: the projection is the rotation's own row 2, so the marker and the
// shot can never disagree, whatever the composition does on combined poses.
void TestCleanAimProjectionFollowsTheRotation() {
    HeadTransform transform;
    float ndcX = 99.0f, ndcY = 99.0f;

    Check(transform.Build(MakePose(0.0f, 0.0f, 15.0f), kCameraUp), "a roll-only pose builds");
    Check(transform.ProjectCleanAim(kFx, kFy, 0.0f, ndcX, ndcY), "a roll-only pose projects");
    CheckNear(ndcX, 0.0f, 1e-5f, "pure roll leaves the aim point at screen centre (x)");
    CheckNear(ndcY, 0.0f, 1e-5f, "pure roll leaves the aim point at screen centre (y)");

    Check(transform.Build(MakePose(0.0f, 12.0f, 0.0f), kCameraUp), "a pitch-only pose builds");
    Check(transform.ProjectCleanAim(kFx, kFy, 0.0f, ndcX, ndcY), "a pitch-only pose projects");
    CheckNear(ndcX, 0.0f, 1e-5f, "pure pitch moves the aim point vertically only");
    Check(ndcY > 0.0f, "pitching up moves the aim point up the screen");

    Check(transform.Build(MakePose(20.0f, 0.0f, 0.0f), kCameraUp), "a yaw-only pose builds");
    Check(transform.ProjectCleanAim(kFx, kFy, 0.0f, ndcX, ndcY), "a yaw-only pose projects");
    CheckNear(ndcY, 0.0f, 1e-5f, "pure yaw moves the aim point horizontally only");
    Check(ndcX < 0.0f, "yawing left moves the aim point the other way");

    // The invariant that keeps the reticle glued to the shot: the projection is
    // the perspective divide of the rotation's own third row.
    Check(transform.Build(MakePose(18.0f, -9.0f, 6.0f), kCameraUp), "a combined pose builds");
    Check(transform.ProjectCleanAim(kFx, kFy, 0.0f, ndcX, ndcY), "a combined pose projects");
    const float* R = transform.Rotation();
    CheckNear(ndcX, kFx * R[8] / R[10], 1e-6f, "the projection is fx * R[8] / R[10]");
    CheckNear(ndcY, kFy * R[9] / R[10], 1e-6f, "the projection is fy * R[9] / R[10]");
}

// Looking far enough away that the clean aim is behind the rotated view, there
// is nowhere on screen to draw it.
void TestAimBehindTheViewIsRejected() {
    HeadTransform transform;
    float ndcX = 0.0f, ndcY = 0.0f;
    Check(transform.Build(MakePose(120.0f, 0.0f, 0.0f), kCameraUp), "an extreme pose builds");
    Check(!transform.ProjectCleanAim(kFx, kFy, 0.0f, ndcX, ndcY),
          "an aim point behind the rotated view is rejected");
    Check(!transform.ProjectCleanAim(0.0f, kFy, 0.0f, ndcX, ndcY), "a degenerate focal term is rejected");
}

// The lean litmus test, and the whole reason the projection takes a distance.
//
// Lean to the right with the head otherwise still: nothing about the picture
// turns, so a direction-only projection leaves the aim point dead centre while
// everything in the world slides left underneath it. The aim POINT has to slide
// with the world, and by more the closer it is.
void TestALeanMovesTheAimPointByTheParallax() {
    HeadPose lean;
    lean.offset[0] = 0.30f;  // the position limit, to the right

    HeadTransform transform;
    Check(transform.Build(lean, kCameraUp), "a lean-only pose builds");

    float ndcX = 99.0f, ndcY = 99.0f;
    Check(transform.ProjectCleanAim(kFx, kFy, 0.0f, ndcX, ndcY), "a lean projects at infinity");
    CheckNear(ndcX, 0.0f, 1e-6f, "a target at infinity does not move when you lean");
    CheckNear(ndcY, 0.0f, 1e-6f, "a target at infinity does not move vertically either");

    // Leaning right moves the eye right, so the target the gun still points at
    // is now to the LEFT of where the picture is centred.
    float nearNdcX = 0.0f, farNdcX = 0.0f;
    Check(transform.ProjectCleanAim(kFx, kFy, 2.0f, nearNdcX, ndcY), "a lean projects at 2m");
    Check(nearNdcX < 0.0f, "leaning right moves the aim point left of centre");
    CheckNear(nearNdcX, -kFx * 0.30f / 2.0f, 1e-6f, "the offset is the parallax angle's tangent");
    CheckNear(ndcY, 0.0f, 1e-6f, "a sideways lean does not move the aim point vertically");

    Check(transform.ProjectCleanAim(kFx, kFy, 8.0f, farNdcX, ndcY), "a lean projects at 8m");
    Check(farNdcX > nearNdcX, "the further the target, the less it moves");
    CheckNear(farNdcX, nearNdcX * 0.25f, 1e-6f, "and it moves in inverse proportion to range");
}

// A rotation-only pose must leave the camera exactly where the game put it.
void TestRotationOnlyLeavesTheCameraPositionAlone() {
    float V[16], X[16], Xinv[16], camPos[3], camPosMoved[3], Vinv[16];
    MakeView(0.6f, 4.0f, -1.0f, 2.0f, V);
    mat::InvertRigid(V, Vinv);

    HeadTransform transform;
    Check(transform.Build(MakePose(15.0f, 5.0f, 0.0f), kCameraUp), "a rotation-only pose builds");
    Check(!transform.HasPositionOffset(), "a rotation-only pose carries no position offset");
    transform.ConjugateForView(V, X, Xinv, camPos, camPosMoved);

    for (int r = 0; r < 3; ++r) {
        CheckNear(camPos[r], Vinv[r * 4 + 3], 1e-5f, "the camera position comes from V^-1");
        CheckNear(camPosMoved[r], camPos[r], 1e-6f, "rotation alone does not move the camera");
    }
}

// X and its inverse have to be genuine inverses, or the forward and inverse
// matrices in one buffer stop describing the same camera.
void TestConjugationProducesInverses() {
    float V[16], X[16], Xinv[16], product[16], I[16], camPos[3], camPosMoved[3];
    mat::Identity(I);
    MakeView(-1.1f, 2.0f, 3.0f, -4.0f, V);

    HeadPose pose = MakePose(22.0f, -14.0f, 7.0f);
    pose.offset[0] = 0.05f;
    pose.offset[1] = -0.03f;
    pose.offset[2] = 0.12f;

    HeadTransform transform;
    Check(transform.Build(pose, kCameraUp), "a six-degree-of-freedom pose builds");
    Check(transform.HasPositionOffset(), "a leaning pose carries a position offset");
    transform.ConjugateForView(V, X, Xinv, camPos, camPosMoved);

    mat::Mul4(X, Xinv, product);
    CheckNear16(product, I, 1e-4f, "X * X^-1 == I");

    // The offset is applied in the ORIGINAL view space, so the camera moves
    // along the clean camera's own axes.
    for (int r = 0; r < 3; ++r) {
        const float expected = camPos[r] + V[r] * pose.offset[0] + V[4 + r] * pose.offset[1] +
                               V[8 + r] * pose.offset[2];
        CheckNear(camPosMoved[r], expected, 1e-5f, "the camera moves along the clean view axes");
    }
}

// The contract the whole injection rests on: post-multiplying a forward matrix
// by X rotates the camera it carries by the head rotation and does nothing else.
// Applied to the view matrix itself, that has to come out as A*V, A being the
// transpose of the head rotation.
void TestPostMultiplyingByXTurnsTheCameraByTheHeadRotation() {
    float V[16], X[16], Xinv[16], camPos[3], camPosMoved[3];
    MakeView(0.35f, 1.0f, 2.0f, 3.0f, V);

    HeadTransform transform;
    Check(transform.Build(MakePose(16.0f, -8.0f, 4.0f), kCameraUp), "a combined pose builds");
    transform.ConjugateForView(V, X, Xinv, camPos, camPosMoved);

    float rotatedView[16];
    mat::Copy16(V, rotatedView);
    mat::PostMul(rotatedView, X);

    float A[16], expected[16];
    mat::Identity(A);
    const float* R = transform.Rotation();
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) A[r * 4 + c] = R[c * 4 + r];
    mat::Mul4(A, V, expected);
    CheckNear16(rotatedView, expected, 1e-4f, "V*X turns the camera by the head rotation");

    // And the matrices that invert the view are put back in step by the other
    // one, so a buffer holding both stays self-consistent.
    float Vinv[16], rotatedInverse[16], expectedInverse[16];
    mat::InvertRigid(V, Vinv);
    mat::Copy16(Vinv, rotatedInverse);
    mat::PreMul(rotatedInverse, Xinv);
    mat::InvertRigid(expected, expectedInverse);
    CheckNear16(rotatedInverse, expectedInverse, 1e-4f, "X^-1 * V^-1 is the inverse of V*X");
}

}  // namespace

int RunHeadTransformTests() {
    tests::Begin("head_transform");
    TestNeutralPosesAreNotInjected();
    TestCameraLocalYawMatchesTheExplicitBuild();
    TestWorldYawReducesToLocalYawWithTheCameraLevel();
    TestRotationStaysOrthonormal();
    TestCleanAimProjectionFollowsTheRotation();
    TestAimBehindTheViewIsRejected();
    TestALeanMovesTheAimPointByTheParallax();
    TestRotationOnlyLeavesTheCameraPositionAlone();
    TestConjugationProducesInverses();
    TestPostMultiplyingByXTurnsTheCameraByTheHeadRotation();
    return tests::g_failures;
}
