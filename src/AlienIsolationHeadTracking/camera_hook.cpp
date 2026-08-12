#include "camera_hook.h"

#include <windows.h>

#include <MinHook.h>

#include "config.h"
#include "constant_buffer_injection.h"
#include "engine_camera_hooks.h"
#include "game_state.h"
#include "head_transform.h"
#include "hud_prompt.h"
#include "injection_state.h"
#include "intro_skip.h"

#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"

#define CAMERAUNLOCK_DX11_OVERLAY_IMPLEMENTATION
#include "cameraunlock/rendering/dx11_overlay.h"

// ===========================================================================
// Head tracking for CATHODE: the tracker pipeline, the reticle, and the wiring
// that installs the injection points.
//
// The injection itself lives in the modules this drives:
//   engine_camera_hooks     - rotating the game's own camera, inside AI.exe
//   constant_buffer_injection - rewriting the matrices on their way to the GPU
//   head_transform          - the pose maths both of them apply
//   injection_state         - what they agree on
//
// The game never observes any of it: aim, raycasts and AI perception read the
// clean camera in every mode.
// ===========================================================================

namespace {
using namespace cameraunlock;

rendering::DX11Overlay g_overlay;
void OverlayLog(const char* msg) { logging::Line("%s", msg); }

HeadTrackingSession<UdpReceiver>* g_session = nullptr;
time::FrameClock g_clock;

// How far the frustum opens when widening is switched on.
constexpr float kWidenedFrustum = 0.6f;

// Small muted grey dot rather than a bright crosshair: it marks the aim point
// without competing with the game's own HUD.
constexpr float kReticleRadius = 3.0f;
constexpr unsigned kReticleColour = 0xC0B0B0B0u;

// How often the diagnostics report, in presented frames.
constexpr int kPoseLogInterval = 600;

// Alt-tabbing pauses the game, and DXGI throttles Present for an inactive
// window - so the render callback, which is the only thing that advances the
// pose, runs far slower than the camera publish that consumes it. The two fall
// out of step and the view alternates between frames built from a stale pose
// and frames built from a fresh one, which is the flicker on tabbing back in.
//
// Publishing no pose at all while the window is inactive removes it: the
// publish finds nothing to apply, and its own revert leaves the camera exactly
// as the game built it. Any foreground window of this process counts, so a
// borderless or exclusive-fullscreen swap does not read as inactive.
bool GameWindowActive() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// Reads the tracker and publishes this frame's pose. False when there is
// nothing fresh, which parks the reticle back at screen centre.
bool UpdatePose(float dt) {
    if (!g_session) return false;

    const bool wasCentered = g_session->HasCentered();
    const bool fresh = g_session->Update(dt);
    if (fresh) {
        camera::HeadPose pose;
        g_session->GetRotation(pose.yaw, pose.pitch, pose.roll);
        if (!g_session->GetPositionOffset(pose.offset[0], pose.offset[1], pose.offset[2])) {
            pose.offset[0] = pose.offset[1] = pose.offset[2] = 0.0f;
        } else {
            // The core clamps z asymmetrically and treats NEGATIVE z as the
            // forward lean (the generous 0.40 limit), positive as the
            // restricted 0.10 backward one. This camera's local +z is forward,
            // so the sign is flipped here, at the boundary between the two
            // conventions. Doing it with invert_z instead would move the
            // generous limit onto the backward lean, which is how it read
            // before: 0.10m of leaning in and 0.40m of pulling away.
            pose.offset[2] = -pose.offset[2];
        }
        camera::State().PublishPose(pose);
    }
    if (!wasCentered && g_session->HasCentered())
        logging::Line("camera: centred on a held pose");
    return fresh;
}

void DrawReticle(rendering::DX11DrawContext& dc, bool aimValid, float ndcX, float ndcY) {
    // The game aims where it always did, so the marker moves to wherever that
    // direction now projects in the rotated view.
    const float cx = dc.Width() * 0.5f;
    const float cy = dc.Height() * 0.5f;
    const float sx = aimValid ? cx + ndcX * cx : cx;
    const float sy = aimValid ? cy - ndcY * cy : cy;
    dc.DrawDot(sx, sy, kReticleRadius, kReticleColour);
}

// The live pose, so a bug report says what the mod was being fed. Deliberately
// no engine counters: buffer uploads and matrices-rewritten only ever meant
// something while the constant-buffer path was being developed, and one of them
// reads as zero in normal play now that the game-camera path is the default,
// which looks like a fault in a log someone sends us.
void ReportProgress() {
    static int frame = 0;
    if ((++frame % kPoseLogInterval) != 0) return;
    const camera::HeadPose pose = camera::State().CurrentPose();
    logging::Line("camera: yaw=%.2f pitch=%.2f roll=%.2f off=(%.3f %.3f %.3f)", pose.yaw,
                  pose.pitch, pose.roll, pose.offset[0], pose.offset[1], pose.offset[2]);
}

void OnRender(rendering::DX11DrawContext& dc) {
    camera::InjectionState& state = camera::State();
    if (dc.Height() > 0)
        camera::constant_buffers::SetRenderAspect(dc.Width() / static_cast<float>(dc.Height()));
    camera::constant_buffers::BeginFrame();

    const float dt = g_clock.Tick();

    // Nothing to track against until the player camera turns up: the game spends
    // its first half-minute on splash screens with no 3D scene at all. Holding
    // the session here also holds its one automatic centre capture, so the
    // earliest it can land is the first 3D frame rather than the intro logos.
    if (!state.HaveReferenceView()) {
        state.ClearPose();
        state.ClearAim();
        return;
    }
    static bool pipelineStarted = false;
    if (!pipelineStarted) {
        pipelineStarted = true;
        logging::Line("camera: player camera live, tracking started");
    }

    // Nothing to track while the player is not in the world. A pause menu draws
    // over a live 3D scene, so without this the view keeps swinging behind it;
    // publishing no pose leaves the publish nothing to apply and its own revert
    // hands the camera back exactly as the game built it.
    if (!GameWindowActive() || camera::game_state::IsPaused()) {
        state.ClearPose();
        state.ClearAim();
        // Put the game's interaction prompt back where it draws it, so the HUD
        // is not left offset behind a menu.
        hud_prompt::Update(0.0f, 0.0f, false);
        return;
    }

    if (!UpdatePose(dt)) {
        state.ClearPose();
        state.ClearAim();
    }

    const bool aimValid = state.AimValid() && state.Enabled();
    float ndcX = 0.0f, ndcY = 0.0f;
    state.GetAim(ndcX, ndcY);
    DrawReticle(dc, aimValid, ndcX, ndcY);

    // The game's own interaction prompt is authored at screen centre; move it
    // onto the same aim point so "E USE" tracks the reticle.
    hud_prompt::Update(ndcX, ndcY, aimValid);

    ReportProgress();
}

}  // namespace

namespace camera {

void Install(UdpReceiver& receiver) {
    // Before the Present hook goes in: its callback runs as soon as the game
    // draws a frame, which can be earlier than the constant-buffer hook setup.
    constant_buffers::Initialize();

    static HeadTrackingSession<UdpReceiver> session(receiver);
    static_assert(HeadTrackingSession<UdpReceiver>::kHasRemoteRecenter,
                  "receiver must forward tracker-app recenter requests");
    g_session = &session;

    State().SetWorldSpaceYaw(config::Get().world_space_yaw);

    // CATHODE's view space runs pitch and roll opposite to OpenTrack's sense,
    // so both are inverted here rather than in the rotation build, which stays
    // a plain description of the engine's axes. Confirmed in game.
    SensitivitySettings sensitivity = SensitivitySettings::Default();
    sensitivity.invert_pitch = true;
    sensitivity.invert_roll = true;
    session.GetProcessor().SetSensitivity(sensitivity);

    // Lean runs opposite to the tracker's sense in this engine's view space,
    // the same way pitch and roll do. Confirmed in game.
    //
    // Z is NOT inverted here. The core-to-engine sign flip lives in UpdatePose,
    // where it belongs, and it keeps the generous forward limit on forward
    // travel; this flag only says which way round the tracker reports a dolly.
    PositionSettings position = PositionSettings::Default();
    position.invert_x = true;
    position.invert_z = false;
    session.GetPositionProcessor().SetSettings(position);

    if (MH_Initialize() != MH_OK) {
        logging::Line("camera: MH_Initialize failed");
        return;
    }
    logging::Line("camera: MinHook initialized");

    rendering::SetDX11OverlayLogger(&OverlayLog);
    g_overlay.SetRenderCallback(&OnRender);
    if (g_overlay.Install()) {
        logging::Line("camera: DX11 Present hook installed");
    } else {
        logging::Line("camera: DX11 Present hook install FAILED");
    }

    // The engine hooks first: they are what actually inject, and the
    // constant-buffer hook's retry loop can block for fifteen seconds.
    intro_skip::Install();
    engine::Install();
    constant_buffers::Install();
}

void Recenter() {
    if (g_session) g_session->Recenter();
}

void SetEnabled(bool enabled) { State().SetEnabled(enabled); }

bool IsEnabled() { return State().Enabled(); }

const char* CycleTrackingMode() {
    if (!g_session) return "unchanged (camera hook not live yet)";
    const TrackingMode mode = g_session->CycleMode();
    State().SetPositionEnabled(mode != TrackingMode::RotationOnly);
    switch (mode) {
        case TrackingMode::RotationOnly: return "ROTATION ONLY (position off)";
        case TrackingMode::PositionOnly: return "POSITION ONLY (rotation off)";
        default: return "ROTATION AND POSITION";
    }
}

bool ToggleYawMode() {
    const bool world = !State().WorldSpaceYaw();
    State().SetWorldSpaceYaw(world);
    logging::Line("Yaw mode: %s", world ? "WORLD-SPACE (horizon-locked)"
                                        : "CAMERA-LOCAL (follows the camera's up axis)");
    return world;
}

bool IsWorldSpaceYaw() { return State().WorldSpaceYaw(); }

void SetFrustumWidening(bool enabled) {
    State().SetFrustumWiden(enabled ? kWidenedFrustum : 1.0f);
}

bool IsFrustumWidening() { return State().FrustumWiden() < 0.999f; }

const char* CycleInjectionMode() {
    const Mode mode = State().CycleMode();
    engine::ClearRotatedViewCache();
    switch (mode) {
        case Mode::CameraSetup: return "CAMERA SETUP (render only)";
        case Mode::ConstantBuffers: return "CONSTANT BUFFERS";
        default: return "GAME CAMERA (culling test - aim follows head)";
    }
}

void Shutdown() {
    g_overlay.Remove();
    MH_Uninitialize();
}

}  // namespace camera
