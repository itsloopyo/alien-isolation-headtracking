#include "injection_state.h"

#include "matrix_math.h"

#include "cameraunlock/logging/file_log.h"

namespace camera {
namespace {

using namespace cameraunlock;

// Namespace scope rather than a function-local static: detours installed by
// Install() reach this from threads of the game's choosing, and a guarded local
// would put a one-time initialisation on that path.
InjectionState g_state;

// The first horizon-locked frame logs how well the clean camera's own up agrees
// with the world up axis, so a wrong axis shows up in the log rather than as a
// mystery in the picture.
void ReportWorldUp(const float* upCam) {
    static bool logged = false;
    if (logged) return;
    logged = true;
    logging::Line("camera: world-space yaw on, world up sits at (% .3f % .3f % .3f) in camera "
                  "space (level camera reads 0 1 0)", upCam[0], upCam[1], upCam[2]);
}

}  // namespace

InjectionState& State() { return g_state; }

Mode InjectionState::CurrentMode() const {
    return static_cast<Mode>(m_mode.load(std::memory_order_relaxed));
}

void InjectionState::SetMode(Mode mode) {
    m_mode.store(static_cast<int>(mode), std::memory_order_relaxed);
}

Mode InjectionState::CycleMode() {
    const int next = (m_mode.load(std::memory_order_relaxed) + 1) % kModeCount;
    m_mode.store(next, std::memory_order_relaxed);
    return static_cast<Mode>(next);
}

bool InjectionState::Enabled() const { return m_enabled.load(std::memory_order_relaxed); }

void InjectionState::SetEnabled(bool enabled) {
    m_enabled.store(enabled, std::memory_order_relaxed);
}

bool InjectionState::PositionEnabled() const {
    return m_positionEnabled.load(std::memory_order_relaxed);
}

void InjectionState::SetPositionEnabled(bool enabled) {
    m_positionEnabled.store(enabled, std::memory_order_relaxed);
}

bool InjectionState::WorldSpaceYaw() const {
    return m_worldSpaceYaw.load(std::memory_order_relaxed);
}

void InjectionState::SetWorldSpaceYaw(bool world) {
    m_worldSpaceYaw.store(world, std::memory_order_relaxed);
}

float InjectionState::FrustumWiden() const {
    return m_frustumWiden.load(std::memory_order_relaxed);
}

void InjectionState::SetFrustumWiden(float scale) {
    m_frustumWiden.store(scale, std::memory_order_relaxed);
}

void InjectionState::PublishPose(const HeadPose& pose) {
    m_yaw = pose.yaw;
    m_pitch = pose.pitch;
    m_roll = pose.roll;
    m_offsetX = pose.offset[0];
    m_offsetY = pose.offset[1];
    m_offsetZ = pose.offset[2];
    m_poseValid = true;
}

void InjectionState::ClearPose() { m_poseValid = false; }

bool InjectionState::PoseValid() const { return m_poseValid; }

HeadPose InjectionState::CurrentPose() const {
    HeadPose pose;
    pose.yaw = m_yaw;
    pose.pitch = m_pitch;
    pose.roll = m_roll;
    pose.offset[0] = m_offsetX;
    pose.offset[1] = m_offsetY;
    pose.offset[2] = m_offsetZ;
    return pose;
}

bool InjectionState::BuildHeadTransform(const float* worldUpCam, HeadTransform& transform) const {
    if (!m_poseValid || !Enabled()) return false;

    HeadPose pose = CurrentPose();
    if (!PositionEnabled()) pose.offset[0] = pose.offset[1] = pose.offset[2] = 0.0f;
    if (pose.IsNeutral()) return false;

    const bool world = WorldSpaceYaw();
    if (world) ReportWorldUp(worldUpCam);
    return transform.Build(pose, world ? worldUpCam : kCameraUp);
}

void InjectionState::PublishAim(float ndcX, float ndcY) {
    m_aimNdcX = ndcX;
    m_aimNdcY = ndcY;
    m_aimValid = true;
}

void InjectionState::ClearAim() { m_aimValid = false; }

bool InjectionState::AimValid() const { return m_aimValid; }

void InjectionState::GetAim(float& ndcX, float& ndcY) const {
    ndcX = m_aimNdcX;
    ndcY = m_aimNdcY;
}

void InjectionState::SetReferenceView(const float* view) {
    mat::Copy16(view, m_referenceView);
    m_haveReferenceView = true;
}

bool InjectionState::HaveReferenceView() const { return m_haveReferenceView; }

const float* InjectionState::ReferenceView() const { return m_referenceView; }

}  // namespace camera
