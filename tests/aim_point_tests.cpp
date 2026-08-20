// Turning a depth-buffer reading into the distance the reticle needs.
//
// The round trip is the test that matters: put a surface at a known distance
// along the clean aim ray, work out what the depth buffer would hold for it, and
// check the same distance comes back. Anything that mis-reads the near plane or
// the storage order fails it by a mile rather than by a subtlety.

#include "aim_point.h"

#include "matrix_math.h"
#include "test_support.h"

namespace {

using namespace camera;
using tests::Check;
using tests::CheckNear;

constexpr float kNear = 0.05f;
constexpr float kFx = 1.4374f;  // the wide projection, measured live
constexpr float kFy = 2.2998f;

// CATHODE's infinite-far projection, in the constant buffers' storage: row
// major, column vectors, translation in the last column.
void MakeProjection(float* P) {
    mat::Identity(P);
    P[0] = kFx;
    P[5] = kFy;
    P[10] = 1.0f;
    P[11] = -kNear;
    P[12] = 0.0f;
    P[13] = 0.0f;
    P[14] = 1.0f;
    P[15] = 0.0f;
}

// What the depth buffer holds for a surface `z` in front of the camera.
float DepthOf(float z) { return 1.0f - kNear / z; }

void TestNearPlaneIsReadFromEitherStorageOrder() {
    float P[16], transposed[16];
    MakeProjection(P);
    mat::Transpose4(P, transposed);

    float nearZ = 0.0f;
    Check(NearPlaneFromProjection(P, nearZ), "the constant-buffer storage order is recognised");
    CheckNear(nearZ, kNear, 1e-6f, "and its near plane comes out");

    nearZ = 0.0f;
    Check(NearPlaneFromProjection(transposed, nearZ), "the camera-setup storage order is too");
    CheckNear(nearZ, kNear, 1e-6f, "and gives the same near plane");
}

// A projection this does not understand must switch the correction off, not
// scale it by whatever happens to sit at the index the near plane usually does.
void TestAnUnrecognisedProjectionIsRejected() {
    float P[16];
    MakeProjection(P);
    P[15] = 1.0f;  // finite far plane: not the shape this reads
    float nearZ = 99.0f;
    Check(!NearPlaneFromProjection(P, nearZ), "a finite-far projection is rejected");

    MakeProjection(P);
    P[11] = -50.0f;  // a "near plane" fifty metres out is not a near plane
    Check(!NearPlaneFromProjection(P, nearZ), "an implausible near plane is rejected");
}

// The constant-buffer path never sees a bare projection, only VP - and the near
// plane survives the multiply, whatever the view is.
void TestNearPlaneSurvivesTheViewMultiply() {
    float P[16], V[16], VP[16];
    MakeProjection(P);
    mat::Identity(V);
    const float c = 0.8f, s = 0.6f;
    V[0] = c;  V[2] = s;
    V[8] = -s; V[10] = c;
    V[3] = 12.5f;
    V[7] = -3.25f;
    V[11] = 40.0f;
    mat::Mul4(P, V, VP);

    float nearZ = 0.0f;
    Check(NearPlaneFromViewProjection(VP, nearZ), "a view-projection yields its near plane");
    CheckNear(nearZ, kNear, 1e-4f, "and it is the projection's own, not the view's translation");
}

AimFrame MakeFrame(float leanX) {
    AimFrame frame;
    frame.fx = kFx;
    frame.fy = kFy;
    frame.nearZ = kNear;
    // With the head not turned, the injected view is the clean one shifted by
    // the lean: the eye the shot leaves from sits the other way from centre.
    frame.eye[0] = -leanX;
    frame.dir[2] = 1.0f;
    frame.valid = true;
    return frame;
}

// Head still, no lean: the aim point is straight ahead, and the distance that
// comes back is the depth it went in as.
void TestDistanceRoundTripsWithNoLean() {
    AimFrame frame = MakeFrame(0.0f);
    for (float expected : {0.9f, 3.0f, 17.5f}) {
        float distance = 0.0f;
        Check(AimDistanceFromDepth(frame, DepthOf(expected), distance),
              "a surface in front of the camera measures");
        CheckNear(distance, expected, expected * 1e-3f, "and its distance round trips");
    }
}

// Leaning: the reticle has already been moved off centre by the parallax, so the
// depth under it is read at that offset point - and the distance that comes back
// must still be the distance along the ray the SHOT travels, not to the pixel.
void TestDistanceRoundTripsThroughALean() {
    constexpr float kLean = 0.30f;
    constexpr float kExpected = 2.5f;
    AimFrame frame = MakeFrame(kLean);
    // Where the aim point projects with the eye leaned: the same offset
    // ProjectCleanAim produces.
    frame.ndcX = kFx * (-kLean) / kExpected;

    float distance = 0.0f;
    Check(AimDistanceFromDepth(frame, DepthOf(kExpected), distance),
          "a leaned frame measures its surface");
    CheckNear(distance, kExpected, kExpected * 1e-3f,
              "and the distance is along the aim ray, not to the pixel");
}

void TestSkyAndNonsenseAreRejected() {
    AimFrame frame = MakeFrame(0.0f);
    float distance = 99.0f;
    Check(!AimDistanceFromDepth(frame, 1.0f, distance), "a sky pixel has no distance");
    Check(!AimDistanceFromDepth(frame, kSkyDepth, distance), "nor has one at the sky threshold");
    Check(!AimDistanceFromDepth(frame, 0.0f, distance), "nor has an untouched depth buffer");

    AimFrame unmeasurable = MakeFrame(0.0f);
    unmeasurable.valid = false;
    Check(!AimDistanceFromDepth(unmeasurable, DepthOf(3.0f), distance),
          "a frame with no near plane measures nothing");
}

}  // namespace

int RunAimPointTests() {
    tests::Begin("aim_point");
    TestNearPlaneIsReadFromEitherStorageOrder();
    TestAnUnrecognisedProjectionIsRejected();
    TestNearPlaneSurvivesTheViewMultiply();
    TestDistanceRoundTripsWithNoLean();
    TestDistanceRoundTripsThroughALean();
    TestSkyAndNonsenseAreRejected();
    return tests::g_failures;
}
