#include "depth_probe.h"

#include <windows.h>
#include <d3d11.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <MinHook.h>

#include "d3d11_context_vtable.h"

#include "cameraunlock/logging/file_log.h"

namespace camera {
namespace depth_probe {
namespace {

using namespace cameraunlock;

typedef void(__stdcall* OMSetRenderTargets_t)(ID3D11DeviceContext*, UINT,
                                              ID3D11RenderTargetView* const*,
                                              ID3D11DepthStencilView*);

OMSetRenderTargets_t g_origOMSetRenderTargets = nullptr;

constexpr int kOMSetRenderTargetsVTableSlot = 33;

// How often a readback is started. The distance it feeds is a world query whose
// answer changes slowly next to a frame, and it is smoothed downstream, so
// asking sixty times a second would copy a depth buffer sixty times to learn
// almost nothing new.
constexpr unsigned long long kProbeIntervalMs = 66;

// The reticle sits on a target, but thin geometry and the edge of anything put
// a wildly different depth one texel away. Nine samples across a few pixels and
// the middle one wins, so a railing between the player and what they are looking
// at cannot pull the distance to itself.
constexpr int kSampleSpacingPx = 3;
constexpr int kSampleGrid = 3;
// A texel at or past this holds no geometry: it is the far end of the range the
// projection maps, which for CATHODE's infinite-far one is the sky.
constexpr float kSurveyFar = 0.9995f;
constexpr int kSampleCount = kSampleGrid * kSampleGrid;

// How the depth is stored, once the format has been read.
enum class Decode { None, Float32, Unorm24, Unorm16, Float32Pair };

CRITICAL_SECTION g_cs;
bool g_initialised = false;

// Every depth buffer the size of the presented frame that was bound this frame,
// in the order they were first bound, with how many times each was bound. A
// frame binds several - a prepass target, the scene's own, whatever a full-res
// effect wants - and which is the scene's is not something a single bind tells
// you, so the choice is made from the whole set.
constexpr int kMaxCandidates = 16;
struct Candidate {
    ID3D11Texture2D* tex = nullptr;  // holds a reference
    int binds = 0;
};
Candidate g_candidates[kMaxCandidates];
int g_candidateCount = 0;

// Depth buffers that were probed and came back with no geometry in them. A
// frame binds more than one full-frame depth target and only one is the
// scene's; which is which is not knowable from the binds alone, so the ones
// that turn out to be empty are struck off and the next candidate is tried.
// Each holds a reference, so an entry can never become a different texture that
// happens to be allocated at the same address.
constexpr int kMaxRejected = 8;
ID3D11Texture2D* g_rejected[kMaxRejected] = {};
int g_rejectedCount = 0;

// The texture the readback in flight was copied from, so the survey's verdict
// lands on the right candidate, and how many surveys in a row have come back
// empty for it. Striking a buffer off on ONE empty survey would be too eager:
// a buffer can be legitimately empty for a frame - a cutscene, a fade, a level
// transition - and the entry is permanent, so a single unlucky frame would cost
// the correction for the rest of the session.
ID3D11Texture2D* g_probed = nullptr;
int g_probedBlanks = 0;
constexpr int kBlanksBeforeRejecting = 3;

bool IsRejected(ID3D11Texture2D* tex) {
    for (int i = 0; i < g_rejectedCount; ++i)
        if (g_rejected[i] == tex) return true;
    return false;
}

void Reject(ID3D11Texture2D* tex) {
    if (!tex || IsRejected(tex) || g_rejectedCount >= kMaxRejected) return;
    tex->AddRef();
    g_rejected[g_rejectedCount++] = tex;
    logging::Line("depth: the buffer at %p holds no geometry; trying the next candidate", tex);
}
volatile LONG g_targetWidth = 0;
volatile LONG g_targetHeight = 0;

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
ID3D11Texture2D* g_staging = nullptr;
UINT g_stagingWidth = 0;
UINT g_stagingHeight = 0;
DXGI_FORMAT g_stagingFormat = DXGI_FORMAT_UNKNOWN;
Decode g_decode = Decode::None;
bool g_copyPending = false;
unsigned long long g_lastProbeMs = 0;

// A staging texture cannot be multisampled and a depth buffer cannot be
// resolved, so an MSAA scene depth cannot be read back at all. Said once, in
// words the player can act on.
void ReportMultisampled(UINT count) {
    static bool logged = false;
    if (logged) return;
    logged = true;
    logging::Line("depth: scene depth is %ux multisampled and cannot be read back; the reticle "
                  "keeps its aim direction but not its distance, so it will drift when you lean. "
                  "Set anti-aliasing to FXAA or SMAA T1x to get the correction back", count);
}

void ReportUnsupportedFormat(DXGI_FORMAT format) {
    static bool logged = false;
    if (logged) return;
    logged = true;
    logging::Line("depth: scene depth format %d is not one this reads; the reticle will drift "
                  "when you lean", static_cast<int>(format));
}

// The format a staging copy of this depth buffer has to be created with, and
// how its texels decode. CopyResource wants formats from the same type group,
// and the typeless member of the group is the one a staging texture is always
// allowed to hold.
bool StagingFormatFor(DXGI_FORMAT format, DXGI_FORMAT& stagingFormat, Decode& decode) {
    switch (format) {
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_R32_FLOAT:
            stagingFormat = DXGI_FORMAT_R32_TYPELESS;
            decode = Decode::Float32;
            return true;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
            stagingFormat = DXGI_FORMAT_R24G8_TYPELESS;
            decode = Decode::Unorm24;
            return true;
        case DXGI_FORMAT_D16_UNORM:
        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_R16_UNORM:
            stagingFormat = DXGI_FORMAT_R16_TYPELESS;
            decode = Decode::Unorm16;
            return true;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R32G8X24_TYPELESS:
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
            stagingFormat = DXGI_FORMAT_R32G8X24_TYPELESS;
            decode = Decode::Float32Pair;
            return true;
        default:
            return false;
    }
}

float DecodeTexel(const uint8_t* row, int x, Decode decode) {
    switch (decode) {
        case Decode::Float32:
            return *reinterpret_cast<const float*>(row + x * 4);
        case Decode::Unorm24:
            return (*reinterpret_cast<const uint32_t*>(row + x * 4) & 0x00FFFFFFu) /
                   16777215.0f;
        case Decode::Unorm16:
            return *reinterpret_cast<const uint16_t*>(row + x * 2) / 65535.0f;
        case Decode::Float32Pair:
            return *reinterpret_cast<const float*>(row + x * 8);
        default:
            return 0.0f;
    }
}

// Records a bind of one depth buffer shaped like the presented frame.
void ConsiderDepthStencil(ID3D11DepthStencilView* dsv) {
    ID3D11Resource* res = nullptr;
    dsv->GetResource(&res);
    if (!res) return;

    ID3D11Texture2D* tex = nullptr;
    if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex)))) {
        D3D11_TEXTURE2D_DESC desc = {};
        tex->GetDesc(&desc);
        // Same shape as the frame, at any scale: CATHODE keeps a half-resolution
        // copy of the scene depth and that is a perfectly good thing to read,
        // but a shadow map or a cube face is square and carries a projection of
        // its own, so its depths would be nonsense the survey could not tell
        // from a scene.
        const bool rightShape =
            desc.Height > 0 && g_targetHeight > 0 &&
            fabsf(static_cast<float>(desc.Width) / desc.Height -
                  static_cast<float>(g_targetWidth) / g_targetHeight) < 0.01f;
        if (rightShape && desc.SampleDesc.Count > 1) ReportMultisampled(desc.SampleDesc.Count);
        if (rightShape && desc.SampleDesc.Count == 1 && !IsRejected(tex)) {
            for (int i = 0; i < g_candidateCount; ++i) {
                if (g_candidates[i].tex == tex) {
                    ++g_candidates[i].binds;
                    tex->Release();
                    tex = nullptr;
                    break;
                }
            }
            if (tex && g_candidateCount < kMaxCandidates) {
                g_candidates[g_candidateCount].tex = tex;  // keeps the reference
                g_candidates[g_candidateCount].binds = 1;
                ++g_candidateCount;
                tex = nullptr;
            }
        }
        if (tex) tex->Release();
    }
    res->Release();
}

void __stdcall OMSetRenderTargetsDetour(ID3D11DeviceContext* ctx, UINT numViews,
                                        ID3D11RenderTargetView* const* rtvs,
                                        ID3D11DepthStencilView* dsv) {
    g_origOMSetRenderTargets(ctx, numViews, rtvs, dsv);
    if (!dsv || g_targetWidth == 0) return;

    EnterCriticalSection(&g_cs);
    __try {
        ConsiderDepthStencil(dsv);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    LeaveCriticalSection(&g_cs);
}

// Which of the frame's candidates is the scene's own depth, and clearing the
// set for the next frame. The one bound most often wins: the scene depth is
// re-bound for every pass that reads or writes it - prepass, opaque,
// transparencies, decals - while a one-off full-res effect target is bound once.
// Candidates already struck off never get here; ConsiderDepthStencil drops them.
ID3D11Texture2D* ClaimFrameDepth() {
    EnterCriticalSection(&g_cs);
    int best = -1;
    for (int i = 0; i < g_candidateCount; ++i)
        if (best < 0 || g_candidates[i].binds > g_candidates[best].binds) best = i;

    // Listed again whenever the choice changes, which is how a rejection shows
    // up in the log as a move to a named buffer rather than as silence.
    static ID3D11Texture2D* lastChoice = nullptr;
    if (best >= 0 && g_candidates[best].tex != lastChoice) {
        lastChoice = g_candidates[best].tex;
        for (int i = 0; i < g_candidateCount; ++i) {
            D3D11_TEXTURE2D_DESC d = {};
            g_candidates[i].tex->GetDesc(&d);
            logging::Line("depth: candidate %d at %p %ux%u fmt %d bound %d times%s", i,
                          g_candidates[i].tex, d.Width, d.Height, static_cast<int>(d.Format),
                          g_candidates[i].binds, i == best ? "  <== chosen" : "");
        }
    }

    ID3D11Texture2D* chosen = nullptr;
    for (int i = 0; i < g_candidateCount; ++i) {
        if (i == best)
            chosen = g_candidates[i].tex;  // hands the reference to the caller
        else
            g_candidates[i].tex->Release();
        g_candidates[i].tex = nullptr;
        g_candidates[i].binds = 0;
    }
    g_candidateCount = 0;
    LeaveCriticalSection(&g_cs);
    return chosen;
}

// The device and its immediate context, taken from the depth buffer itself.
// GetImmediateContext is what matters here: the detour above can run on a
// deferred context, where a copy would be recorded into a command list instead
// of executed, and the readback would never complete.
bool EnsureDevice(ID3D11Texture2D* source) {
    if (g_context) return true;
    source->GetDevice(&g_device);
    if (!g_device) return false;
    g_device->GetImmediateContext(&g_context);
    return g_context != nullptr;
}

bool EnsureStaging(const D3D11_TEXTURE2D_DESC& src) {
    DXGI_FORMAT stagingFormat = DXGI_FORMAT_UNKNOWN;
    Decode decode = Decode::None;
    if (!StagingFormatFor(src.Format, stagingFormat, decode)) {
        ReportUnsupportedFormat(src.Format);
        return false;
    }
    if (g_staging && g_stagingWidth == src.Width && g_stagingHeight == src.Height &&
        g_stagingFormat == stagingFormat)
        return true;

    if (g_staging) {
        g_staging->Release();
        g_staging = nullptr;
    }
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = src.Width;
    desc.Height = src.Height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = stagingFormat;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &g_staging))) {
        logging::Line("depth: staging texture %ux%u format %d could not be created; the reticle "
                      "will drift when you lean", src.Width, src.Height,
                      static_cast<int>(stagingFormat));
        g_staging = nullptr;
        return false;
    }
    g_stagingWidth = src.Width;
    g_stagingHeight = src.Height;
    g_stagingFormat = stagingFormat;
    g_decode = decode;
    logging::Line("depth: reading scene depth back from a %ux%u %s buffer", src.Width, src.Height,
                  decode == Decode::Float32     ? "32-bit float"
                  : decode == Decode::Unorm24   ? "24-bit"
                  : decode == Decode::Unorm16   ? "16-bit"
                                                : "32-bit float + stencil");
    return true;
}

// The middle depth of a small patch around the point. Sorting nine values is
// cheaper than reasoning about which of them is the target.
float MedianDepthAt(const D3D11_MAPPED_SUBRESOURCE& mapped, int px, int py) {
    float samples[kSampleCount];
    int n = 0;
    for (int dy = -(kSampleGrid / 2); dy <= kSampleGrid / 2; ++dy) {
        const int y = std::min(std::max(py + dy * kSampleSpacingPx, 0),
                               static_cast<int>(g_stagingHeight) - 1);
        const uint8_t* row = static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch;
        for (int dx = -(kSampleGrid / 2); dx <= kSampleGrid / 2; ++dx) {
            const int x = std::min(std::max(px + dx * kSampleSpacingPx, 0),
                                   static_cast<int>(g_stagingWidth) - 1);
            samples[n++] = DecodeTexel(row, x, g_decode);
        }
    }
    std::sort(samples, samples + n);
    return samples[n / 2];
}

// Whether a readback holds a rendered scene at all. A depth buffer the game
// never drew into comes back uniform - all near, all far - and a probe of one
// texel cannot tell that from a wall right in front of the player. A coarse
// sweep of the whole image can, and it is what decides whether the candidate
// this frame chose is the scene's depth or some other full-res target.
constexpr int kSurveyGrid = 16;
constexpr int kSurveyMinVaried = kSurveyGrid * kSurveyGrid / 20;  // 5%

bool SurveyLooksLikeAScene(const D3D11_MAPPED_SUBRESOURCE& mapped) {
    int varied = 0;
    float lo = 1.0f, hi = 0.0f;
    for (int gy = 0; gy < kSurveyGrid; ++gy) {
        const int y = gy * (g_stagingHeight - 1) / (kSurveyGrid - 1);
        const uint8_t* row = static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch;
        for (int gx = 0; gx < kSurveyGrid; ++gx) {
            const int x = gx * (g_stagingWidth - 1) / (kSurveyGrid - 1);
            const float d = DecodeTexel(row, x, g_decode);
            if (d > 0.0f && d < kSurveyFar) ++varied;
            if (d < lo) lo = d;
            if (d > hi) hi = d;
        }
    }
    static ID3D11Texture2D* lastLogged = nullptr;
    if (lastLogged != g_probed) {
        lastLogged = g_probed;
        logging::Line("depth: survey of the buffer at %p: %d of %d texels hold geometry, "
                      "range %.6f to %.6f", g_probed, varied, kSurveyGrid * kSurveyGrid, lo, hi);
    }
    return varied >= kSurveyMinVaried;
}

bool CollectReadback(bool wantSample, float ndcX, float ndcY, float& depth) {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hr =
        g_context->Map(g_staging, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (hr == DXGI_ERROR_WAS_STILL_DRAWING) return false;  // the copy is still running
    g_copyPending = false;
    if (FAILED(hr)) return false;

    const bool scene = SurveyLooksLikeAScene(mapped);
    g_probedBlanks = scene ? 0 : g_probedBlanks + 1;
    if (g_probedBlanks >= kBlanksBeforeRejecting) Reject(g_probed);
    bool got = false;
    if (scene && wantSample && fabsf(ndcX) <= 1.0f && fabsf(ndcY) <= 1.0f) {
        const int px = static_cast<int>((ndcX * 0.5f + 0.5f) * (g_stagingWidth - 1));
        const int py = static_cast<int>((0.5f - ndcY * 0.5f) * (g_stagingHeight - 1));
        depth = MedianDepthAt(mapped, px, py);
        got = true;
    }
    g_context->Unmap(g_staging, 0);
    return got;
}

void StartReadback(ID3D11Texture2D* frameDepth) {
    D3D11_TEXTURE2D_DESC desc = {};
    frameDepth->GetDesc(&desc);
    if (!EnsureDevice(frameDepth) || !EnsureStaging(desc)) return;
    g_context->CopyResource(g_staging, frameDepth);
    if (g_probed != frameDepth) g_probedBlanks = 0;
    if (g_probed) g_probed->Release();
    g_probed = frameDepth;
    g_probed->AddRef();
    g_copyPending = true;
}

}  // namespace

void Install() {
    InitializeCriticalSection(&g_cs);
    g_initialised = true;

    void** vt = nullptr;
    if (!GetContextVTable(vt)) {
        logging::Line("depth: context vtable probe FAILED; the reticle will drift when you lean");
        return;
    }
    void* target = vt[kOMSetRenderTargetsVTableSlot];
    if (MH_CreateHook(target, &OMSetRenderTargetsDetour,
                      reinterpret_cast<void**>(&g_origOMSetRenderTargets)) != MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        logging::Line("depth: OMSetRenderTargets hook FAILED; the reticle will drift when you "
                      "lean");
        return;
    }
    logging::Line("depth: OMSetRenderTargets hook installed");
}

bool Update(int frameWidth, int frameHeight, bool wantSample, float ndcX, float ndcY,
            float& depth) {
    if (!g_initialised || !g_origOMSetRenderTargets) return false;
    InterlockedExchange(&g_targetWidth, frameWidth);
    InterlockedExchange(&g_targetHeight, frameHeight);

    bool got = false;
    if (g_copyPending) got = CollectReadback(wantSample, ndcX, ndcY, depth);

    ID3D11Texture2D* frameDepth = ClaimFrameDepth();

    const unsigned long long now = GetTickCount64();
    if (frameDepth && !g_copyPending && now - g_lastProbeMs >= kProbeIntervalMs) {
        g_lastProbeMs = now;
        __try {
            StartReadback(frameDepth);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (frameDepth) frameDepth->Release();
    return got;
}

void Shutdown() {
    if (!g_initialised) return;
    if (g_staging) {
        g_staging->Release();
        g_staging = nullptr;
    }
    if (g_probed) {
        g_probed->Release();
        g_probed = nullptr;
    }
    for (int i = 0; i < g_rejectedCount; ++i) g_rejected[i]->Release();
    g_rejectedCount = 0;
    if (g_context) {
        g_context->Release();
        g_context = nullptr;
    }
    if (g_device) {
        g_device->Release();
        g_device = nullptr;
    }
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_candidateCount; ++i) {
        g_candidates[i].tex->Release();
        g_candidates[i].tex = nullptr;
    }
    g_candidateCount = 0;
    LeaveCriticalSection(&g_cs);
    DeleteCriticalSection(&g_cs);
    g_initialised = false;
}

}  // namespace depth_probe
}  // namespace camera
