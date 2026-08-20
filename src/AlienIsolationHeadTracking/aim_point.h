#pragma once

// Where the shot lands, as a POINT rather than a direction.
//
// While the head is only turned, the eye the frame is drawn from and the eye
// the shot leaves from are the same place, and a direction is all the reticle
// needs. A lean separates them: the picture is drawn from an eye a hand's width
// to one side of the one the bullet comes from, so a fixed direction projects to
// a screen position that slides off the thing it is meant to be marking. That is
// parallax, and it is worse the closer the target - `atan(lean / distance)`, so
// about 17 degrees at the 0.30m lean clamp and a metre away.
//
// Correcting it needs the distance, and this is how the mod gets one: the depth
// buffer already holds it, at the very pixel the reticle is drawn on. Reading
// one texel back is cheaper than a physics query and needs no pinned address,
// so it keeps working across game patches.
//
// Pure: an AimFrame and a depth value in, a distance out. The D3D11 side of the
// readback lives in depth_probe.

namespace camera {

// What the reticle projection needs to know about one rendered frame, captured
// where the engine hands over the camera. Carried as data rather than read back
// out of the injection points, because the readback that turns it into a
// distance only lands a frame or two later, by which time those have moved on.
//
// `eye` and `dir` are in the coordinates of the view the frame was RENDERED
// with - the leaned, head-turned one - which is the frame the depth buffer is
// in too.
struct AimFrame {
    float fx = 0.0f, fy = 0.0f;       // the projection's focal terms
    float nearZ = 0.0f;               // its near plane, in world units
    float ndcX = 0.0f, ndcY = 0.0f;   // where the clean aim was drawn
    float eye[3] = {0.0f, 0.0f, 0.0f};  // the eye the shot leaves from
    float dir[3] = {0.0f, 0.0f, 1.0f};  // the direction it leaves along
    bool valid = false;
};

// Depth at or past this reads as sky. With CATHODE's 0.05 near plane it is a
// hundred metres out, where a 0.30m lean moves the aim point by a sixth of a
// degree - far below a pixel, and below the point of correcting anything.
constexpr float kSkyDepth = 0.9995f;

// The near plane of a CATHODE projection, whichever way round it is stored.
//
// The engine's projections are infinite-far, so the depth row is (0,0,1,-near)
// and the w row is (0,0,1,0). Two of those four elements are 1 and one is
// -near, and WHICH INDEX holds the near plane is the whole difference between
// the constant-buffer storage order and the camera-setup one. Returns false for
// anything that is not that shape, so a projection this does not recognise
// switches the correction off rather than scaling it by a number that is not a
// near plane.
bool NearPlaneFromProjection(const float* proj, float& nearZ);

// The same near plane, read out of a view-projection instead - the only form
// the constant-buffer path ever sees a projection in.
//
// VP = P*V with P's last two rows (0,0,1,-near) and (0,0,1,0), so VP's last two
// rows are both V's forward row, one of them with -near added to its w term.
// Subtracting them leaves the near plane and nothing else, whatever V is. Row-
// major, column-vector storage, which is what the constant buffers use.
bool NearPlaneFromViewProjection(const float* vp, float& nearZ);

// How far along the clean aim ray the surface under the reticle sits.
//
// `depth` is the depth buffer's own value at the frame's (ndcX, ndcY). The
// projection is infinite-far with `depth = 1 - near/z`, so that surface is at
// view distance `near / (1 - depth)` and the focal terms place it sideways.
// What the reticle needs is where that point falls along the ray the SHOT
// travels, which starts from the un-leaned eye - so the answer is the
// projection of the surface point onto that ray.
//
// False for a sky pixel, where there is no surface and the direction projection
// is already the right answer, and for a surface behind the eye.
bool AimDistanceFromDepth(const AimFrame& frame, float depth, float& outDistance);

}  // namespace camera
