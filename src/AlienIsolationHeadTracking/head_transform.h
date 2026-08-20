#pragma once

// The head pose as the engine has to see it, and nothing else.
//
// Let S be the head transform expressed in view space. The rotated view matrix
// is V' = S*V, so for any forward matrix M = (something)*V:
//
//   M' = (something)*S*V = M * (V^-1 * S * V) = M * X
//
// and for any inverse matrix N = V^-1*(something):
//
//   N' = X^-1 * N
//
// One X, post-multiplied into everything carrying the view and pre-multiplied
// into everything inverting it, keeps the whole set self-consistent. A bare P
// carries no view component and is left alone.
//
// Pure: matrices and a pose in, matrices out. No engine state, no logging.

namespace camera {

// World up in this engine's world space. CATHODE is Y-up.
constexpr float kWorldUp[3] = {0.0f, 1.0f, 0.0f};

// The camera's own up axis, in camera coordinates - the yaw axis that makes head
// yaw turn about whatever the camera currently calls up.
constexpr float kCameraUp[3] = {0.0f, 1.0f, 0.0f};

// kWorldUp expressed in camera coordinates. V's rows are the camera's
// world-space axes, so V * kWorldUp is that vector in camera coordinates.
void WorldUpInCameraSpace(const float* V, float* upCam);

// The head rotation, composed in view-local axes: yaw about yawAxisCam, pitch
// about right, roll about forward. Roll is negated to match OpenTrack's
// convention, as everywhere else in the catalogue.
//
// yawAxisCam picks between the two yaw modes and is the whole difference
// between them. Horizon-locked yaw passes the world up expressed in camera
// coordinates; camera-local yaw passes kCameraUp. With the camera level the two
// are the same vector and the two modes coincide exactly. Pitch and roll stay
// camera-local either way, so a head tilt still works about the axis the player
// is looking down.
void BuildHeadRotation(float yawDeg, float pitchDeg, float rollDeg, const float* yawAxisCam,
                       float* R);

// Degrees of head rotation and metres of head translation, after the tracking
// pipeline has processed them.
struct HeadPose {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float offset[3] = {0.0f, 0.0f, 0.0f};

    // Close enough to centred that injecting it would be a no-op.
    bool IsNeutral() const;
};

class HeadTransform {
public:
    // Builds S and its inverse for this pose. False for a neutral pose, where
    // there is nothing to inject and the object is left untouched.
    bool Build(const HeadPose& pose, const float* yawAxisCam);

    // The camera rotation itself, for callers that rotate an orientation rather
    // than a matrix (the game camera entity's quaternion).
    const float* Rotation() const { return m_rotation; }

    // Whether the pose carries a translation, and so whether a camera position
    // published beside the matrices needs moving to match.
    bool HasPositionOffset() const { return m_hasOffset; }

    // The translation in camera-local axes (x right, y up, z forward), for
    // callers that move a camera position directly rather than through S - the
    // game camera entity, whose orientation is a quaternion and so cannot carry
    // a translation at all.
    const float* Offset() const { return m_offset; }

    // X = V^-1 * S * V, built from the view matrix the caller's matrices were
    // derived from, plus the camera position before and after the head offset.
    //
    // Using one shared reference view instead is wrong the moment the game
    // camera has turned since that reference was captured: the conjugation then
    // sits in the wrong frame and the rendered view snaps by the difference.
    // Standing still it is exact, so the artifact appears only while the mouse
    // is turning - which is precisely how it was reported.
    void ConjugateForView(const float* V, float* X, float* Xinv, float* camPos,
                          float* camPosMoved) const;

    // Where the clean aim lands on screen after the injection, in normalised
    // device coordinates. fx and fy are the projection's focal terms.
    //
    // `aimDistance` is how far along the clean aim ray the thing being aimed at
    // sits, in world units, and is what makes a LEAN come out right: the frame
    // is drawn from an eye the lean has moved, so a point at finite range no
    // longer projects where its direction does. Zero means "no distance known",
    // which is the honest answer for a shot into the sky and is what the rest of
    // the pipeline passes until the depth readback lands.
    //
    // False when the aim point is behind the injected view or the focal terms
    // are degenerate.
    bool ProjectCleanAim(float fx, float fy, float aimDistance, float& ndcX, float& ndcY) const;

    // The clean aim ray, in the coordinates of the view the frame is RENDERED
    // with - the eye the shot leaves from, and the direction it leaves along.
    // Together with a distance they give the aim POINT, which is what the
    // reticle actually has to sit on.
    const float* CleanEyeInView() const { return m_cleanEye; }
    const float* CleanAimInView() const { return m_cleanAim; }

private:
    float m_rotation[16] = {};
    // S = A * T(-offset), A being the inverse of the camera rotation (rotating
    // the camera by R moves view-space points by R^-1). The translation is
    // applied in the ORIGINAL view space, before the head rotation, so a lean
    // follows the body's orientation rather than the head's. S does not depend
    // on which camera it is applied to, so it is built once and conjugated per
    // matrix.
    float m_s[16] = {};
    float m_sInverse[16] = {};
    float m_offset[3] = {};
    // The clean view's origin and forward, carried into the injected view by S.
    // Both fall straight out of S and R, and are kept so no caller has to
    // re-derive the composition and risk disagreeing with it.
    float m_cleanEye[3] = {};
    float m_cleanAim[3] = {0.0f, 0.0f, 1.0f};
    bool m_hasOffset = false;
};

}  // namespace camera
