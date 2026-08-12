#pragma once

// Deciding what a 4x4 float block found in a constant buffer actually is, and
// recovering the camera it describes.
//
// Nothing here keys off a buffer size or a fixed offset: CATHODE spreads the
// camera across several constant buffers and the same matrices reappear in more
// than one of them, so every 16-byte offset of every buffer is classified from
// its own contents. All of it is pure - matrices in, verdict out - which is what
// makes it testable off a game process.
//
// Layout convention is matrix_math.h's: float[16], row-major, translation at
// f[3], f[7], f[11].

namespace camera {

// Shadow maps and cubemap faces render square; a display never is. Comparing
// against the backbuffer aspect would be tighter, but the 3D scene does not
// always share the swapchain's shape, and requiring a match rejected the real
// camera outright.
bool PlausibleCameraAspect(float aspect);

// Bare projection: the w row is (0,0,+/-1,0). A view-projection's w row is the
// view matrix's forward row and carries the view-space z translation, which is
// never zero in practice, so this only ever matches a standalone P. Those must
// be left alone: they map view space to clip, and view space is exactly what
// the injection redefines, so an unmodified P stays consistent with it.
bool IsBareProjection(const float* f);

// Identify the geometry view-projection VP = P*V (P perspective, V rigid).
// Two properties pin it down independent of camera rotation AND field of view:
//  - Projective: neither the 4th row nor the 4th column is (0,0,0,1). Affine
//    view/world matrices have exactly one of those; a VP/P has neither.
//  - Two of the four row xyz-magnitudes are unit (~1.0): the depth row and the
//    perspective/forward row. V is orthonormal so these magnitudes survive any
//    camera rotation; FOV changes only scale the focal rows.
bool IsViewProjection(const float* f);

// Rigid view matrix: bottom row (0,0,0,1) and an orthonormal upper-left 3x3.
bool IsRigidView(const float* f);

// Recovers the view matrix a view-projection was built from. VP = P*V, and P's
// rows are (fx,0,0,0) (0,fy,0,0) (0,0,1,-near) (0,0,1,0), so VP's w row IS V's
// forward row and the first two rows are V's right and up rows scaled by the
// focal terms. Exact, and same-frame: no cached view matrix from another
// buffer, and no chance of pairing one camera's projection with another's view.
bool RecoverView(const float* vp, float* V, float& aspect);

// Rebuilds the view matrix an inverse view-projection was derived from.
// inv(VP) = V^-1 * P^-1, so its first two 3x3 columns are the view's right and
// up rows scaled by 1/fx and 1/fy, its third column is -(1/near) * cameraPos,
// and m[15] is 1/near. Forward is right x up, and the translation follows from
// the position. Exact, and from the matrix alone - no shared reference needed.
bool RecoverViewFromInverse(const float* m, float* V);

// Same camera, to within a frame of movement. Comparing all three basis rows
// and the position rejects reflection probes, shadow cameras and cubemap faces,
// which a single forward-axis test lets through whenever one happens to point
// near the player's view direction.
bool SameCamera(const float* a, const float* b);

// Identifies a matrix that maps back OUT of this camera's clip space, from its
// structure alone rather than by pairing it with a forward matrix.
//
// This matters because the deferred lighting and shadow buffers carry the
// camera inverse WITHOUT the matching view-projection beside it. Pairing can
// never find those, so they kept reconstructing world positions from an
// un-rotated camera while the geometry had already rotated - which is what
// drags shadows around as the head moves.
//
// inv(VP) = V^-1 * P^-1, and P^-1's w row is (0, 0, -1/near, 1/near). That last
// pair is the discriminator: an inverse's w row reads (0, 0, -k, k) with k > 0,
// which a forward projection (w row (0,0,1,0)) can never satisfy. Without it,
// a projection and an inverse are indistinguishable whenever the camera is
// axis-aligned, since both then carry a diagonal leading 3x3.
//
// The rest: P^-1 has only 1/fx and 1/fy in its first two columns, so the
// product's first two 3x3 columns are the view's right and up rows, scaled.
// Matching their directions against the reference camera is what ties the
// matrix to the player's view rather than a light's or a probe's.
bool LooksLikeInverseOfCamera(const float* m, const float* V);

// The camera-world matrix, V^-1: rigid, its 3x3 is the view's 3x3 transposed
// and its translation is the camera position. A deferred pass that
// reconstructs view-space position from depth uses this to reach world space,
// so leaving it un-rotated drags everything derived from world position -
// shadows above all - while the geometry has already turned.
//
// It needs its own test because it is an inverse with a (0,0,0,1) w row, which
// the inverse-view-projection signature deliberately excludes.
bool LooksLikeCameraWorld(const float* m, const float* V);

}  // namespace camera
