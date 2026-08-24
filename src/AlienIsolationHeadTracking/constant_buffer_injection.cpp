#include "constant_buffer_injection.h"

#include <windows.h>
#include <d3d11.h>

#include <cmath>
#include <cstdint>

#include <MinHook.h>

#include "aim_point.h"
#include "camera_matrix.h"
#include "d3d11_context_vtable.h"
#include "head_transform.h"
#include "injection_state.h"
#include "mapped_buffer_table.h"
#include "matrix_math.h"

#if defined(AIHT_CAMERA_SCAN)
#include "camera_scan.h"
#endif

#include "cameraunlock/logging/file_log.h"

namespace camera {
namespace constant_buffers {
namespace {

using namespace cameraunlock;

typedef HRESULT(__stdcall* Map_t)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT,
                                  D3D11_MAPPED_SUBRESOURCE*);
typedef void(__stdcall* Unmap_t)(ID3D11DeviceContext*, ID3D11Resource*, UINT);

Map_t g_origMap = nullptr;
Unmap_t g_origUnmap = nullptr;

CRITICAL_SECTION g_cs;
MappedBufferTable g_mapped;

volatile float g_renderAspect = 0.0f;

// A camera buffer is at least one matrix and no bigger than the largest the
// engine publishes a camera in; beyond that it is material or instance data.
constexpr UINT kMinBufferBytes = 64;
constexpr UINT kMaxBufferBytes = 2048;
// Constant buffers pack float4 rows, so a matrix can begin at any 16-byte
// offset and nothing may be assumed about a buffer from its first one.
constexpr int kFloatsPerRow = 4;
constexpr int kMatrixFloats = 16;
constexpr int kMaxHits = 16;

// How a hit's own view matrix is recovered, so the conjugation is never built
// from a stale shared reference.
enum HitKind {
    kForwardVP,     // view-projection: recover V from it, post-multiply by X
    kForwardView,   // the view matrix itself, post-multiply by X
    kInverseVP,     // inverse view-projection: recover V from it, pre-multiply by X^-1
    kInverseWorld,  // camera-world (V^-1): invert for V, pre-multiply by X^-1
};

struct Hit {
    int off;  // offset into the buffer, in floats
    HitKind kind;
};

bool IsInverseHit(HitKind kind) { return kind == kInverseVP || kind == kInverseWorld; }

void Append(Hit* hits, int& count, int off, HitKind kind) {
    hits[count].off = off;
    hits[count].kind = kind;
    ++count;
}

// Every matrix-shaped thing found in one buffer, whether or not it turns out to
// belong to the player camera. Kept so the inverse scan cannot re-claim a
// projection that the camera match already rejected.
struct BufferScan {
    int candOff[kMaxHits];
    float recovered[kMaxHits][16];
    float candAspect[kMaxHits];
    int candCount = 0;

    Hit claimed[kMaxHits];
    int claimedCount = 0;
};

// Within a frame the reference camera may only be refreshed by the same camera,
// never swapped for a different one: a shadow map is rendered by drawing the
// scene from a LIGHT's point of view, so the engine uploads a buffer that looks
// exactly like a camera. Accepting one of those means rotating the shadow map
// render itself by the head pose, which drags every shadow in the scene around
// while the main view stays correct.
bool g_refLockedThisFrame = false;

bool Overlaps(const Hit* hits, int count, int off) {
    for (int i = 0; i < count; ++i)
        if (off > hits[i].off - kMatrixFloats && off < hits[i].off + kMatrixFloats) return true;
    return false;
}

bool IsConstantBuffer(ID3D11Resource* res, UINT& sizeOut) {
    ID3D11Buffer* buf = nullptr;
    if (FAILED(res->QueryInterface(__uuidof(ID3D11Buffer), reinterpret_cast<void**>(&buf))) || !buf)
        return false;
    D3D11_BUFFER_DESC d;
    buf->GetDesc(&d);
    buf->Release();
    if (!(d.BindFlags & D3D11_BIND_CONSTANT_BUFFER)) return false;
    sizeOut = d.ByteWidth;
    return true;
}

// Camera buffers are reported once per distinct (size, what-was-found)
// signature. Menus upload the same buffers with every matrix set to identity,
// so a size alone would spend its one report on a frame that says nothing;
// keying on the signature means the first populated frame reports too.
constexpr int kMaxReportedSignatures = 16;

struct Reported {
    UINT size;
    uint32_t signature;
};
Reported g_reported[kMaxReportedSignatures];
int g_reportedCount = 0;

bool AlreadyReported(UINT size, uint32_t signature) {
    for (int i = 0; i < g_reportedCount; ++i)
        if (g_reported[i].size == size && g_reported[i].signature == signature) return true;
    if (g_reportedCount >= kMaxReportedSignatures) return true;
    g_reported[g_reportedCount].size = size;
    g_reported[g_reportedCount].signature = signature;
    ++g_reportedCount;
    return false;
}

// Forward view-projections. A camera view-projection's w row IS the view
// matrix's forward row, so it is unit length - one cheap test that rejects
// essentially every non-camera buffer before any real work happens.
void FindForwardCandidates(const float* cb, int nf, BufferScan& scan) {
    for (int o = 0; o + kMatrixFloats <= nf && scan.candCount < kMaxHits; o += kFloatsPerRow) {
        const float* m = cb + o;
        if (fabsf(mat::Mag3(m[12], m[13], m[14]) - 1.0f) > 0.01f) continue;
        if (!mat::Finite(m, kMatrixFloats) || !IsViewProjection(m)) continue;
        if (!RecoverView(m, scan.recovered[scan.candCount], scan.candAspect[scan.candCount]))
            continue;
        scan.candOff[scan.candCount] = o;
        ++scan.candCount;
        Append(scan.claimed, scan.claimedCount, o, kForwardVP);
        o += kMatrixFloats - kFloatsPerRow;  // a matched matrix occupies the next three rows too
    }
}

// A buffer that states the view matrix outright, but only when it is actually
// the player's camera and not a light's. A torch is a spotlight sitting at the
// camera position pointing down the camera forward, so its shadow view matrix is
// identical to the player's. Its aspect is not.
int FindExplicitPlayerView(const float* cb, int nf, const BufferScan& scan,
                           const InjectionState& state) {
    for (int o = 0; o + kMatrixFloats <= nf; o += kFloatsPerRow) {
        const float* m = cb + o;
        if (!mat::Finite(m, kMatrixFloats) || mat::IsIdentity(m) || !IsRigidView(m)) continue;
        for (int c = 0; c < scan.candCount; ++c) {
            if (!SameCamera(scan.recovered[c], m)) continue;
            if (!PlausibleCameraAspect(scan.candAspect[c])) continue;
            if (g_refLockedThisFrame && !SameCamera(m, state.ReferenceView())) continue;
            return o;
        }
    }
    return -1;
}

void ReportNoReference(const BufferScan& scan) {
    static bool logged = false;
    if (logged || scan.candCount == 0) return;
    logged = true;
    logging::Line("camera: no reference yet (backbuffer aspect=%.4f, candidates:)", g_renderAspect);
    for (int c = 0; c < scan.candCount; ++c)
        logging::Line("  cand @ +0x%03X aspect=%.4f", scan.candOff[c] * 4, scan.candAspect[c]);
}

// Every matrix in this buffer that belongs to the player camera: the forward
// view-projections that match the reference, the explicit view matrix, and the
// matrices mapping back out of this camera's clip space.
int CollectHits(const float* cb, int nf, BufferScan& scan, const float* refV, int vOff, Hit* hits,
                int& aimOff) {
    int hitCount = 0;
    aimOff = -1;

    // Drop projections belonging to some other camera - shadow, reflection and
    // cubemap passes all publish their own.
    for (int c = 0; c < scan.candCount; ++c) {
        if (!SameCamera(scan.recovered[c], refV)) continue;
        if (!PlausibleCameraAspect(scan.candAspect[c])) continue;
        if (aimOff < 0) aimOff = scan.candOff[c];
        Append(hits, hitCount, scan.candOff[c], kForwardVP);
    }

    if (vOff >= 0 && !Overlaps(scan.claimed, scan.claimedCount, vOff) && hitCount < kMaxHits) {
        Append(hits, hitCount, vOff, kForwardView);
        if (scan.claimedCount < kMaxHits) Append(scan.claimed, scan.claimedCount, vOff, kForwardView);
    }

    for (int o = 0; o + kMatrixFloats <= nf && hitCount < kMaxHits; o += kFloatsPerRow) {
        const float* m = cb + o;
        if (Overlaps(scan.claimed, scan.claimedCount, o)) continue;
        if (!mat::Finite(m, kMatrixFloats)) continue;
        const bool invVP = LooksLikeInverseOfCamera(m, refV);
        if (!invVP && !LooksLikeCameraWorld(m, refV)) continue;
        Append(hits, hitCount, o, invVP ? kInverseVP : kInverseWorld);
        o += kMatrixFloats - kFloatsPerRow;
    }
    return hitCount;
}

// What was claimed in a buffer, once per distinct shape. The raw float rows this
// used to dump alongside were how the layouts in .lab/NOTES.md were worked out;
// they are written down there now, and a buffer's worth of them per shape is a
// few hundred lines nobody reads in a log sent with a bug report.
void ReportBuffer(UINT size, const Hit* hits, int hitCount, int vOff) {
    const uint32_t signature = static_cast<uint32_t>(hitCount) | (vOff >= 0 ? 0x80000000u : 0u);
    if (AlreadyReported(size, signature)) return;
    logging::Line("=== camera CB sz=%u ===", size);
    for (int h = 0; h < hitCount; ++h)
        logging::Line("  %s @ +0x%03X", IsInverseHit(hits[h].kind) ? "inverse" : "forward",
                      hits[h].off * 4);
}

// One view for the whole buffer, recovered from this buffer's own matrices.
//
// Per-HIT recovery is wrong even though each recovery is exact: a buffer's
// inverse can be a frame apart from its forward, so conjugating each by its own
// view makes them disagree with each other and the disagreement flickers frame
// to frame. A single shared GLOBAL view is wrong the other way - it goes stale
// the moment the game camera turns. The buffer is the scope that is both
// self-consistent and current.
bool RecoverBufferView(const float* cb, const Hit* hits, int hitCount, float* V) {
    for (int h = 0; h < hitCount; ++h) {
        const float* m = cb + hits[h].off;
        float aspect;
        if (hits[h].kind == kForwardView) {
            mat::Copy16(m, V);
            return true;
        }
        if (hits[h].kind == kForwardVP && RecoverView(m, V, aspect)) return true;
    }
    for (int h = 0; h < hitCount; ++h) {
        const float* m = cb + hits[h].off;
        if (hits[h].kind == kInverseVP && RecoverViewFromInverse(m, V)) return true;
        if (hits[h].kind == kInverseWorld) {
            mat::InvertRigid(m, V);
            return true;
        }
    }
    return false;
}

// The camera position also ships as a bare float3 beside the matrices (constant
// buffers pack float4 rows, so it sits on a 16-byte boundary), and shaders
// reconstruct world position from it. A positional offset that moves the camera
// without updating it leaves everything world-derived sitting at the old place.
void MoveCameraPosition(float* cb, int nf, const Hit* hits, int hitCount, const float* camPos,
                        const float* camPosMoved) {
    for (int o = 0; o + 3 <= nf; o += kFloatsPerRow) {
        if (Overlaps(hits, hitCount, o)) continue;
        if (fabsf(cb[o] - camPos[0]) > 1e-3f || fabsf(cb[o + 1] - camPos[1]) > 1e-3f ||
            fabsf(cb[o + 2] - camPos[2]) > 1e-3f)
            continue;
        cb[o] = camPosMoved[0];
        cb[o + 1] = camPosMoved[1];
        cb[o + 2] = camPosMoved[2];
    }
}

// Applies the head transform to every player-camera matrix in one constant
// buffer.
void InjectIntoCB(float* cb, UINT size) {
    // In game-camera mode the publish is already rotated, so the engine turns
    // the view itself - rotating the GPU copy as well turns the image twice
    // while culling turns once, and the frustum stops matching what is drawn.
    // Camera-setup injection covers everything this does, and doing both would
    // apply the head rotation twice. Kept switchable so they can be compared.
    InjectionState& state = State();
    if (state.CurrentMode() != Mode::ConstantBuffers) return;
    if (size < kMinBufferBytes || size > kMaxBufferBytes) return;
    const int nf = static_cast<int>(size) / 4;

    BufferScan scan;
    FindForwardCandidates(cb, nf, scan);

    const int vOff = FindExplicitPlayerView(cb, nf, scan, state);
    if (vOff >= 0) {
        state.SetReferenceView(cb + vOff);
        g_refLockedThisFrame = true;
#if defined(AIHT_CAMERA_SCAN)
        // Driven from here so the probe only runs once a real player camera has
        // turned up, rather than through the menus.
        camera_scan::Update();
#endif
    }
    if (!state.HaveReferenceView()) {
        ReportNoReference(scan);
        return;
    }

    Hit hits[kMaxHits];
    int aimOff = -1;
    const int hitCount = CollectHits(cb, nf, scan, state.ReferenceView(), vOff, hits, aimOff);
    if (hitCount == 0) return;

    ReportBuffer(size, hits, hitCount, vOff);

    float upCam[3];
    WorldUpInCameraSpace(state.ReferenceView(), upCam);
    HeadTransform head;
    if (!state.BuildHeadTransform(upCam, head)) {
        // Nothing is being injected, so the clean aim direction is the middle of
        // the screen again. Leaving the last offset published parks the reticle
        // off centre over an un-rotated view.
        state.ClearAim();
        return;
    }

    // Focal terms come from the view-projection itself: VP = P*V with V
    // orthonormal, so the xyz-magnitude of VP row 0 is fx and of row 1 is fy.
    if (aimOff >= 0) {
        const float* vp = cb + aimOff;
        AimFrame frame;
        frame.fx = mat::Mag3(vp[0], vp[1], vp[2]);
        frame.fy = mat::Mag3(vp[4], vp[5], vp[6]);
        if (head.ProjectCleanAim(frame.fx, frame.fy, state.AimDistance(), frame.ndcX, frame.ndcY)) {
            for (int i = 0; i < 3; ++i) {
                frame.eye[i] = head.CleanEyeInView()[i];
                frame.dir[i] = head.CleanAimInView()[i];
            }
            frame.valid = NearPlaneFromViewProjection(vp, frame.nearZ);
            state.PublishAim(frame);
        } else {
            state.ClearAim();
        }
    }

    float V[16];
    if (!RecoverBufferView(cb, hits, hitCount, V)) return;

    float X[16], Xinv[16], camPos[3], camPosMoved[3];
    head.ConjugateForView(V, X, Xinv, camPos, camPosMoved);

    for (int h = 0; h < hitCount; ++h) {
        float* m = cb + hits[h].off;
        if (IsInverseHit(hits[h].kind)) mat::PreMul(m, Xinv);
        else mat::PostMul(m, X);
    }

    if (head.HasPositionOffset()) MoveCameraPosition(cb, nf, hits, hitCount, camPos, camPosMoved);
}

HRESULT __stdcall MapDetour(ID3D11DeviceContext* ctx, ID3D11Resource* res, UINT sub, D3D11_MAP type,
                            UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped) {
    HRESULT hr = g_origMap(ctx, res, sub, type, flags, mapped);
    if (SUCCEEDED(hr) && mapped && mapped->pData && sub == 0) {
        UINT size = 0;
        if (IsConstantBuffer(res, size) && size >= kMinBufferBytes) {
            EnterCriticalSection(&g_cs);
            if (!g_mapped.Insert(res, mapped->pData, size)) {
                static bool warned = false;
                if (!warned) {
                    warned = true;
                    logging::Line("camera: mapped-buffer table full (%d); constant-buffer "
                                  "injection is dropping uploads",
                                  MappedBufferTable::kCapacity);
                }
            }
            LeaveCriticalSection(&g_cs);
        }
    }
    return hr;
}

void __stdcall UnmapDetour(ID3D11DeviceContext* ctx, ID3D11Resource* res, UINT sub) {
    if (sub == 0) {
        EnterCriticalSection(&g_cs);
        MappedBufferTable::Record rec;
        if (g_mapped.Take(res, rec))
            InjectIntoCB(reinterpret_cast<float*>(rec.data), rec.size);
        LeaveCriticalSection(&g_cs);
    }
    g_origUnmap(ctx, res, sub);
}

constexpr int kMapVTableSlot = 14;
constexpr int kUnmapVTableSlot = 15;
// Steam's overlay hooks the same Map/Unmap slots, so installation is a load
// order race it can lose; retry until our detour sticks.
constexpr int kInstallAttempts = 30;
constexpr DWORD kInstallRetryMs = 500;

}  // namespace

void Initialize() { InitializeCriticalSection(&g_cs); }

void Install() {
    void** vt = nullptr;
    if (!GetContextVTable(vt)) {
        logging::Line("camera: context vtable probe FAILED");
        return;
    }
    void* mapTarget = vt[kMapVTableSlot];
    void* unmapTarget = vt[kUnmapVTableSlot];
    for (int attempt = 0; attempt < kInstallAttempts; ++attempt) {
        MH_RemoveHook(mapTarget);
        MH_RemoveHook(unmapTarget);
        MH_STATUS cm = MH_CreateHook(mapTarget, &MapDetour, reinterpret_cast<void**>(&g_origMap));
        MH_STATUS cu =
            MH_CreateHook(unmapTarget, &UnmapDetour, reinterpret_cast<void**>(&g_origUnmap));
        MH_STATUS em = MH_EnableHook(mapTarget);
        MH_STATUS eu = MH_EnableHook(unmapTarget);
        if (cm == MH_OK && cu == MH_OK && em == MH_OK && eu == MH_OK) {
            logging::Line("camera: Map/Unmap hooks installed (attempt %d)", attempt + 1);
            return;
        }
        Sleep(kInstallRetryMs);
    }
    logging::Line("camera: constant-buffer hook FAILED after retries (Steam overlay likely won "
                  "the race); source injection is unaffected");
}

void BeginFrame() {
    // Both this and the constant-buffer hook run on the render thread, so the
    // flag needs no lock.
    g_refLockedThisFrame = false;
}

void SetRenderAspect(float aspect) { g_renderAspect = aspect; }

}  // namespace constant_buffers
}  // namespace camera
