#pragma once

#include <atomic>

#include "aim_point.h"
#include "head_transform.h"

// The state the injection points and the Present-hook callback that feeds them
// share: which point is live, the pose to inject, where the clean aim lands,
// and the player camera everything is matched against.
//
// One owner for all of it, because the injection points must agree: only one
// may rotate, or the head pose is applied twice, and a stale pose published by
// one path would be picked up by another. The pose and aim fields are written
// on the render thread and read on the render thread one step later; the toggles
// are written by the hotkey thread.

namespace camera {

// Where the head pose is injected.
//
// GameCamera rotates the game's own camera entity across the camera publish,
// which is the only injection point the engine's visibility work actually sees,
// so it is the default. CameraSetup rotates the view matrix handed to the
// engine's camera setup, downstream of culling. ConstantBuffers rewrites the
// matrices on their way to the GPU and is the only point that works on a build
// whose pinned addresses we do not know. The others remain reachable for
// comparison.
enum class Mode { CameraSetup, ConstantBuffers, GameCamera };
constexpr int kModeCount = 3;

class InjectionState {
public:
    Mode CurrentMode() const;
    void SetMode(Mode mode);
    Mode CycleMode();

    bool Enabled() const;
    void SetEnabled(bool enabled);

    bool PositionEnabled() const;
    void SetPositionEnabled(bool enabled);

    // Horizon-locked yaw, the default. Off, yaw turns about whatever the camera
    // is currently calling up, which leans and rolls the view once the player is
    // looking steeply up or down.
    bool WorldSpaceYaw() const;
    void SetWorldSpaceYaw(bool world);

    // Scales the focal terms of the projection handed to the engine. Below 1.0
    // the frustum widens. If CATHODE culls against this projection, widening it
    // makes the engine submit the geometry that currently gets cut off at the
    // edges of a head-turned view; the picture zooms out to match, which is the
    // cost of finding out. Toggled live so the effect can be judged directly.
    float FrustumWiden() const;
    void SetFrustumWiden(float scale);

    void PublishPose(const HeadPose& pose);
    void ClearPose();
    bool PoseValid() const;
    HeadPose CurrentPose() const;

    // Builds this frame's head transform for the injection point calling it.
    // worldUpCam is the world up axis expressed in the CLEAN camera's
    // coordinates, refreshed by the caller immediately before this call - it is
    // the yaw axis in horizon-locked mode and ignored otherwise. False when
    // tracking is off, no pose has arrived, or the pose is neutral.
    bool BuildHeadTransform(const float* worldUpCam, HeadTransform& transform) const;

    // Screen-space position of the clean aim point in the rotated view,
    // published by whichever injection point sees the live focal terms and drawn
    // by the Present hook. The frame carries the rest of what the depth readback
    // needs to turn a depth value at that point into a distance.
    void PublishAim(const AimFrame& frame);
    void ClearAim();
    bool AimValid() const;
    void GetAim(float& ndcX, float& ndcY) const;
    AimFrame CurrentAimFrame() const;

    // How far along the clean aim ray the thing being aimed at sits, in world
    // units, or zero for "not known" - which projects the aim as a direction, as
    // it did before there was a readback. Measured by the Present hook one frame
    // and consumed by the injection points the next.
    void SetAimDistance(float distance);
    float AimDistance() const;

    // The player camera, taken from whichever hook states it outright. Every
    // other candidate matrix is matched against it, so a lighting buffer holding
    // nothing but an inverse is still tied to the right camera. Nothing runs
    // until one exists: the game spends its first half-minute on splash screens
    // with no 3D scene at all.
    void SetReferenceView(const float* view);
    bool HaveReferenceView() const;
    const float* ReferenceView() const;

private:
    // Toggles are atomic: the hotkey thread writes them while the render thread
    // reads them. Relaxed is enough - each stands alone and none of them orders
    // any other memory.
    std::atomic<int> m_mode{static_cast<int>(Mode::GameCamera)};
    std::atomic<bool> m_enabled{true};
    std::atomic<bool> m_positionEnabled{true};
    std::atomic<bool> m_worldSpaceYaw{true};
    std::atomic<float> m_frustumWiden{1.0f};

    // Pose, aim and reference view never leave the render thread: the Present
    // callback publishes them and the injection points read them one step later.
    volatile float m_yaw = 0.0f, m_pitch = 0.0f, m_roll = 0.0f;
    volatile float m_offsetX = 0.0f, m_offsetY = 0.0f, m_offsetZ = 0.0f;
    volatile bool m_poseValid = false;

    AimFrame m_aimFrame;
    volatile bool m_aimValid = false;
    volatile float m_aimDistance = 0.0f;

    float m_referenceView[16] = {};
    bool m_haveReferenceView = false;
};

InjectionState& State();

}  // namespace camera
