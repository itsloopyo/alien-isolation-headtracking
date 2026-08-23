#include "engine_camera_hooks.h"

#include <windows.h>

#include <cmath>
#include <cstdint>

#include <MinHook.h>

#include "aim_point.h"
#include "build_profile.h"
#include "camera_matrix.h"
#include "config.h"
#include "head_transform.h"
#include "injection_state.h"
#include "matrix_math.h"

#include "cameraunlock/logging/file_log.h"

namespace camera {
namespace engine {
namespace {

using namespace cameraunlock;

typedef int(__cdecl* FrameTask_t)();
typedef void(__fastcall* CameraInputs_t)(void*, void*);
typedef void(__fastcall* CameraDerive_t)(float*, void*);
typedef void(__fastcall* CameraSetup_t)(void*, void*, void*, float*, float*, float*);

CameraInputs_t g_origCameraInputs = nullptr;
CameraDerive_t g_origCameraDerive = nullptr;
CameraSetup_t g_origCameraSetup = nullptr;

uintptr_t ModuleBase() { return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)); }

bool CreateAndEnable(uintptr_t rva, void* detour, void** original) {
    void* target = reinterpret_cast<void*>(ModuleBase() + rva);
    return MH_CreateHook(target, detour, original) == MH_OK && MH_EnableHook(target) == MH_OK;
}

// --- the game's camera entity ---------------------------------------------

// Orientation quaternion (x, y, z, w) of the game's camera entity, saved so it
// can be put back.
float g_lastWritten[4];
float g_lastHead[4];
// The game's own camera orientation for this frame, kept so the cached forward
// can be rebuilt from it after the publish has derived a rotated one.
float g_cleanQuat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
// The entity's own position, and the moved one we wrote, saved so the move can
// be taken back off exactly like the rotation. A quaternion cannot carry a
// translation, so without this the game-camera path is rotation-only: the pose
// still has 6DOF, but the lean never reaches the rendered view.
float g_cleanPos[3] = {};
float g_lastWrittenPos[3] = {};
bool g_entityMoved = false;
void* g_savedEntity = nullptr;
bool g_entityRotated = false;
bool g_entityLogged = false;
// The transform the last successful entity rotation was built from. The camera
// setup hook reprojects the clean aim through it, so it outlives the publish and
// is deliberately never cleared: a frame with no fresh pose keeps aiming through
// the last one, exactly as the rotation itself does. Zeroed until the first
// rotation, which reads as "behind the camera" and suppresses the reticle.
HeadTransform g_entityHead;
// Cached as the publisher resolves it, so the render window can reach it.
void* g_cameraEntity = nullptr;

// The entity the camera publish reads the camera from. It picks a primary unless
// a secondary is flagged live, which is the behaviour observed by watching both
// fields as the game switches cameras.
void* ResolveCameraEntity(void* self) {
    const builds::OffsetTable::CameraEntity& e = builds::ActiveOffsets().entity;
    const uintptr_t o = reinterpret_cast<uintptr_t>(self);
    __try {
        uintptr_t primary = *reinterpret_cast<uintptr_t*>(o + e.primary);
        uintptr_t chosen = 0;
        if (primary && *reinterpret_cast<char*>(primary + e.primary_enabled) &&
            !*reinterpret_cast<char*>(primary + e.primary_suppressed))
            chosen = primary;
        if (*reinterpret_cast<char*>(o + e.secondary_live) &&
            *reinterpret_cast<uintptr_t*>(o + e.secondary))
            chosen = *reinterpret_cast<uintptr_t*>(o + e.secondary);
        return reinterpret_cast<void*>(chosen);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

float* EntityOrientation(void* entity) {
    return reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(entity) +
                                    builds::ActiveOffsets().entity.orientation);
}

float* EntityPosition(void* entity) {
    return reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(entity) +
                                    builds::ActiveOffsets().entity.position);
}

// Reports the entity's layout once. The orientation and position offsets start
// as inferences from static analysis, and this is what confirms them against the
// running game.
void ReportCameraEntity(void* entity) {
    if (g_entityLogged || !entity) return;
    g_entityLogged = true;
    const builds::OffsetTable& offsets = builds::ActiveOffsets();
    const float* quat = EntityOrientation(entity);
    const float* pos = reinterpret_cast<const float*>(reinterpret_cast<uintptr_t>(entity) +
                                                      offsets.entity.position);
    __try {
        const float qlen = mat::QuatLength(quat);
        logging::Line("entity: +0x%X % .4f % .4f % .4f % .4f  (len %.4f)",
                      static_cast<unsigned>(offsets.entity.orientation), quat[0], quat[1], quat[2],
                      quat[3], qlen);
        logging::Line("entity: +0x%X % .4f % .4f % .4f",
                      static_cast<unsigned>(offsets.entity.position), pos[0], pos[1], pos[2]);
        const float* eye =
            reinterpret_cast<const float*>(ModuleBase() + offsets.render_camera.published_eye);
        logging::Line("entity: published eye % .4f % .4f % .4f", eye[0], eye[1], eye[2]);
        logging::Line("entity: orientation is %s",
                      mat::IsUnitQuat(quat) ? "a unit quaternion" : "NOT a unit quaternion");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logging::Line("entity: fields not readable");
    }
}

// Writes the game's own orientation back. Paired with RotateEntity().
void RevertEntityToClean() {
    if (!g_entityRotated || !g_savedEntity) return;
    __try {
        float* q = EntityOrientation(g_savedEntity);
        if (mat::SameQuat(q, g_lastWritten)) {
            float inv[4], clean[4];
            mat::QuatConjugate(g_lastHead, inv);
            mat::QuatMul(q, inv, clean);
            for (int i = 0; i < 4; ++i) q[i] = clean[i];
        }
        if (g_entityMoved) {
            // Only put the saved position back if it is still the one we wrote.
            // The game moves the player between our calls, and restoring a stale
            // position would teleport them - the same hazard the orientation
            // guard above exists for.
            float* p = EntityPosition(g_savedEntity);
            if (mat::SameVec3(p, g_lastWrittenPos))
                for (int i = 0; i < 3; ++i) p[i] = g_cleanPos[i];
            g_entityMoved = false;
        }
        g_entityRotated = false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_entityMoved = false;
        g_entityRotated = false;
    }
}

// Says why the rotation is or is not being applied, once per distinct reason,
// instead of leaving "nothing moved" to be interpreted. Latched per reason
// rather than counted: the reason flips every frame around the neutral pose
// (BuildHeadTransform refuses a neutral pose, so a player holding still
// alternates between "applied" and "no head pose yet" at frame rate), and a
// budget of reason changes is spent in under a second - long before "faulted"
// or "quaternion not unit" ever happen. Every call site passes a string
// literal, so the pointer identifies the reason.
constexpr int kMaxRotateReasons = 12;

void ReportRotate(const char* why) {
    static const char* seen[kMaxRotateReasons] = {};
    static int count = 0;
    for (int i = 0; i < count; ++i)
        if (seen[i] == why) return;
    if (count < kMaxRotateReasons) seen[count++] = why;
    logging::Line("camera: rotate -> %s", why);
}

void RotateEntity(void* entity) {
    InjectionState& state = State();
    if (state.CurrentMode() != Mode::GameCamera) {
        ReportRotate("skipped, not game-camera mode");
        return;
    }
    if (!entity) { ReportRotate("skipped, no camera entity cached yet"); return; }
    if (g_entityRotated) { ReportRotate("skipped, already rotated"); return; }

    __try {
        float* q = EntityOrientation(entity);
        if (!mat::Finite(q, 4)) return;

        // Whatever the entity holds now is the game's own orientation: the
        // guard above only lets this run while nothing of ours is applied, and
        // RevertEntityToClean() is what takes it back off. Restoring a saved
        // copy instead would be wrong - the game writes a fresh orientation
        // from the mouse between our calls, and putting the old one back would
        // overwrite the player's look entirely.
        float clean[4] = {q[0], q[1], q[2], q[3]};
        if (!mat::IsUnitQuat(clean)) { ReportRotate("skipped, quaternion not unit"); return; }

        // World up in camera coordinates, taken from the game's own orientation
        // - the conjugate carries a world vector into camera space. The head
        // transform built next reads it for the world-space yaw axis.
        float conj[4], upCam[3];
        mat::QuatConjugate(clean, conj);
        mat::QuatRotate(conj, kWorldUp, upCam);

        if (!state.BuildHeadTransform(upCam, g_entityHead)) {
            ReportRotate("skipped, no head pose yet");
            return;
        }

        for (int i = 0; i < 4; ++i) g_cleanQuat[i] = clean[i];

        float headQuat[4], rotated[4];
        mat::QuatFromMatrix(g_entityHead.Rotation(), headQuat);
        mat::QuatMul(clean, headQuat, rotated);  // head rotation in camera-local axes
        for (int i = 0; i < 4; ++i) {
            q[i] = rotated[i];
            g_lastWritten[i] = rotated[i];
            g_lastHead[i] = headQuat[i];
        }
        // The lean. The entity's orientation is a quaternion, so the offset
        // cannot ride along with the rotation the way it does in S - it has to
        // be added to the position separately. It is expressed in camera-local
        // axes and the CLEAN orientation carries it into world space, so a lean
        // follows the body rather than the head, matching the buffer path.
        if (g_entityHead.HasPositionOffset()) {
            float* p = EntityPosition(entity);
            if (mat::Finite(p, 3)) {
                float world[3];
                mat::QuatRotate(clean, g_entityHead.Offset(), world);
                for (int i = 0; i < 3; ++i) {
                    g_cleanPos[i] = p[i];
                    p[i] = g_cleanPos[i] + world[i];
                    g_lastWrittenPos[i] = p[i];
                }
                g_entityMoved = true;
            }
        }

        g_savedEntity = entity;
        g_entityRotated = true;
        ReportRotate("applied");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_entityRotated = false;
        ReportRotate("faulted");
    }
}

// --- the cached forward gameplay aims with ---------------------------------

float* g_playerCamObj = nullptr;
bool g_forwardCorrected = false;

// The engine caches its camera forward as the camera's own +z carried into
// world space, so the orientation it was derived from is enough to reproduce it.
void CachedForwardFrom(const float* quat, float* out) {
    const float forward[3] = {0.0f, 0.0f, 1.0f};
    mat::QuatRotate(quat, forward, out);
}

// Writes the cached forward as either the head-rotated direction or the game's
// own. Culling consumes this value as well as gameplay, so it cannot simply be
// left clean - it has to be rotated while the cull set is built and clean the
// rest of the time.
void SetCachedForward(bool rotated) {
    if (!g_playerCamObj) return;
    // Only while this mode is actually rotating the camera. Toggled off - or in
    // any other injection mode - the engine's own derived forward is already
    // the clean one, and stamping the last head rotation over it every frame
    // leaves aim skewed for as long as tracking stays off. Dropping the cached
    // object makes the derive hook re-acquire it when injection resumes.
    if (State().CurrentMode() != Mode::GameCamera || !State().Enabled()) {
        g_playerCamObj = nullptr;
        return;
    }
    __try {
        const int slot = builds::ActiveOffsets().render_camera.cached_forward_float;
        float v[3];
        CachedForwardFrom(rotated ? g_lastWritten : g_cleanQuat, v);
        g_playerCamObj[slot] = v[0];
        g_playerCamObj[slot + 1] = v[1];
        g_playerCamObj[slot + 2] = v[2];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_playerCamObj = nullptr;
    }
}

// --- frame tasks -----------------------------------------------------------

struct FrameTask {
    const char* name;
    FrameTask_t original;
    bool seen;
    // Gameplay tasks only: whether this one runs with the head-rotated camera.
    // Exactly one does - see g_cleanTasks.
    bool keepsRotation;
};

// Observation only: each of these logs once when it first runs, which is how we
// know the cull set really is built after the camera publish rotated the entity
// (and therefore that no separate bracket around these tasks is needed).
FrameTask g_renderTasks[builds::kRenderTaskCount] = {
    {"CALCULATE_POVS", nullptr, false, false},
    {"BUILD_PRE_MAIN_RENDER_LISTS", nullptr, false, false},
    {"BUILD_MAIN_RENDER_LIST", nullptr, false, false},
    {"BUILD_SHADOWS_RENDER_LISTS", nullptr, false, false},
};

// ENTITY_MANAGER is the one task the head rotation is put back on for, because
// it is where geometry attached to the camera gets placed - the space suit
// helmet above all. Run off the body's camera like everything else, the helmet
// faces where the body looks, and a head turn puts the unlit back of the shell
// across half the screen. Confirmed by bracketing each task in turn in a suit
// section: only this one moves the helmet, and rotating for the other seven
// changes nothing.
FrameTask g_cleanTasks[builds::kCleanTaskCount] = {
    {"CONTROLLER_UPDATE", nullptr, false, false},
    {"HAVOK_PROCESS_RAYCASTS", nullptr, false, false},
    {"AI_START_THINK", nullptr, false, false},
    {"AI_END_THINK", nullptr, false, false},
    {"ENTITY_MANAGER", nullptr, false, true},
    {"ENTITY_MANAGER_TICK", nullptr, false, false},
    {"PICKUP_MANAGER_TICK", nullptr, false, false},
    {"MAIN_THREAD_CHARACTER_PROCESSING", nullptr, false, false},
};

void ReportTaskOnce(FrameTask& task, const char* what) {
    if (task.seen) return;
    task.seen = true;
    logging::Line("camera: %s %s", task.name, what);
}

int RunRenderTask(int index) {
    FrameTask& task = g_renderTasks[index];
    if (!task.original) return 0;
    ReportTaskOnce(task, "runs with the rotated camera");
    return task.original();
}

int RunCleanTask(int index) {
    FrameTask& task = g_cleanTasks[index];
    if (!task.original) return 0;
    ReportTaskOnce(task, task.keepsRotation ? "runs with the rotated camera"
                                            : "reads the clean forward");
    // The entity is clean for the whole frame outside this, so this is the ONE
    // place that puts the rotation back on, around the ONE task that has to see
    // it. Which way round that is stated matters: the rotation used to stand
    // for the rest of the frame and be lifted around each gameplay task, which
    // left it applied through every stretch of the frame that is not one of
    // these eight - and something in there places the first-person weapon.
    const bool rotate = config::Get().helmet_follows_head && task.keepsRotation;
    SetCachedForward(rotate);
    if (rotate) RotateEntity(g_cameraEntity);
    const int result = task.original();
    if (rotate) RevertEntityToClean();
    SetCachedForward(true);
    return result;
}

// One __cdecl thunk per task, because the engine calls them with no argument to
// say which one it is.
template <int I>
int __cdecl RenderTaskDetour() { return RunRenderTask(I); }
template <int I>
int __cdecl CleanTaskDetour() { return RunCleanTask(I); }

FrameTask_t kRenderDetours[builds::kRenderTaskCount] = {
    &RenderTaskDetour<0>, &RenderTaskDetour<1>, &RenderTaskDetour<2>, &RenderTaskDetour<3>};

FrameTask_t kCleanDetours[builds::kCleanTaskCount] = {
    &CleanTaskDetour<0>, &CleanTaskDetour<1>, &CleanTaskDetour<2>, &CleanTaskDetour<3>,
    &CleanTaskDetour<4>, &CleanTaskDetour<5>, &CleanTaskDetour<6>, &CleanTaskDetour<7>};

void InstallFrameTaskHooks(FrameTask* tasks, const FrameTask_t* detours, const uintptr_t* rvas,
                           int count, const char* what) {
    for (int i = 0; i < count; ++i) {
        if (!CreateAndEnable(rvas[i], detours[i], reinterpret_cast<void**>(&tasks[i].original))) {
            logging::Line("camera: %s hook FAILED for %s", what, tasks[i].name);
            tasks[i].original = nullptr;
        }
    }
}

// --- detours ---------------------------------------------------------------

void __fastcall CameraInputsDetour(void* self, void* edx) {
    // Culling has only ever followed the head when the PUBLISH itself is
    // rotated - holding the rotation for the rest of the frame does nothing for
    // it. So rotate before the publish, and revert as soon as it returns: the
    // matrices are built and uploaded by then, so the view and the cull set keep
    // the rotation, while the entity goes back to the game's own orientation for
    // the rest of the frame.
    RevertEntityToClean();
    g_cameraEntity = ResolveCameraEntity(self);
    RotateEntity(g_cameraEntity);
    g_origCameraInputs(self, edx);
    // The rotation has done its work here: the view and the cull set are built
    // from it by the time the publish returns. Take it straight back off.
    //
    // It used to stand until the next publish, so that the entity update could
    // place the space suit helmet off a head-turned camera. That left it applied
    // through every stretch of the frame that is not one of the hooked gameplay
    // tasks, and something in there places the first-person weapon: the weapon
    // then followed head PITCH instead of the body. Measured - at 20 degrees of
    // head pitch the world moved 435 pixels and the weapon 40. Yaw and roll
    // looked right, which is what made it survive so long: the weapon takes its
    // yaw from the body and only its pitch from the camera, so the one axis that
    // leaked is the one axis a screenshot makes hardest to read.
    //
    // The helmet keeps working because RunCleanTask puts the rotation back on
    // around ENTITY_MANAGER alone, which is the only task that ever needed it
    // (Session 9 bisected exactly that).
    RevertEntityToClean();
    ReportCameraEntity(g_cameraEntity);
}

void __fastcall CameraDeriveDetour(float* obj, void* edx) {
    g_origCameraDerive(obj, edx);
    if (State().CurrentMode() != Mode::GameCamera || !g_entityRotated || !obj) return;
    __try {
        // Seven callers reach this, most building other points of view -
        // shadows, reflections. Only the camera whose cached forward is the one
        // we just rotated is the player's; the rest must keep what the engine
        // derived for them.
        const int slot = builds::ActiveOffsets().render_camera.cached_forward_float;
        float rotated[3];
        CachedForwardFrom(g_lastWritten, rotated);
        if (fabsf(obj[slot] - rotated[0]) > 1e-3f || fabsf(obj[slot + 1] - rotated[1]) > 1e-3f ||
            fabsf(obj[slot + 2] - rotated[2]) > 1e-3f)
            return;

        // Leave the value exactly as the engine derived it - rotated. Culling
        // is correct that way, and it is the state the game expects. The clean
        // direction is handed out only inside the gameplay tasks.
        g_playerCamObj = obj;
        if (!g_forwardCorrected) {
            g_forwardCorrected = true;
            logging::Line("camera: cached forward is clean for gameplay, rotated for culling");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Said once: the near plane is the whole scale of the depth readback, so a
// projection this could not read shows up in the log rather than as a reticle
// that quietly stops correcting for a lean.
void ReportNearPlane(bool known, float nearZ) {
    static bool logged = false;
    if (logged) return;
    logged = true;
    if (known)
        logging::Line("camera: projection near plane is %.4f; the reticle can be corrected for a "
                      "lean", nearZ);
    else
        logging::Line("camera: the projection is not the infinite-far shape this reads a near "
                      "plane from; the reticle will drift when you lean");
}

// The last view the camera setup hook handed back, so a re-submission of it can
// be recognised. See RecognisePlayerCamera.
float g_lastRotatedView[16];
bool g_haveLastRotatedView = false;
bool g_sourceLogged = false;

// Publishes where the clean aim lands in the frame about to be drawn, together
// with everything the depth readback needs to turn a depth value at that point
// into a distance. The distance it projects with is the one that readback
// measured a frame or two ago - the loop that keeps the reticle on its target
// through a lean.
void PublishAimFor(InjectionState& state, const HeadTransform& head, const float* proj) {
    AimFrame frame;
    frame.fx = proj[0];
    frame.fy = proj[5];
    if (!head.ProjectCleanAim(frame.fx, frame.fy, state.AimDistance(), frame.ndcX, frame.ndcY)) {
        state.ClearAim();
        return;
    }
    for (int i = 0; i < 3; ++i) {
        frame.eye[i] = head.CleanEyeInView()[i];
        frame.dir[i] = head.CleanAimInView()[i];
    }
    // Without a near plane a depth value cannot be turned into a distance at
    // all, so the frame is published un-measurable rather than measured wrong,
    // and the reticle falls back to projecting the aim as a direction.
    frame.valid = NearPlaneFromProjection(proj, frame.nearZ);
    ReportNearPlane(frame.valid, frame.nearZ);
    state.PublishAim(frame);
}

// Whether these arguments are the player's own camera, and if so publishing it.
// Rejects square projections (shadow maps and cubemap faces), non-rigid views,
// and a view this hook itself rotated - several of the setup's callers re-submit
// the camera's own stored view, and rotating it again applies the head pose
// twice, so anything drawn from that path swings at double speed.
//
// The publishing happens in EVERY mode, because the render callback holds the
// whole pipeline until a player camera exists and this is the only place that
// learns of one. V receives the view in constant-buffer orientation.
bool RecognisePlayerCamera(const float* view, const float* proj, float* V) {
    if (!view || !proj) return false;
    const float fx = proj[0], fy = proj[5];
    if (fx <= 1e-4f || fy <= 1e-4f || !PlausibleCameraAspect(fy / fx)) return false;
    if (g_haveLastRotatedView && mat::SameMatrix(view, g_lastRotatedView)) return false;
    mat::Transpose4(view, V);
    if (!IsRigidView(V)) return false;

    InjectionState& state = State();
    state.SetReferenceView(V);

    // Where the clean aim ray lands on screen. The game aims down the camera's
    // un-rotated forward and draws its own marker at screen centre, but once
    // the head is turned that direction no longer projects to the centre of
    // what is rendered - so the marker has to move to where it actually points.
    // The rotation is the one the camera publish just applied to the entity, and
    // the projection's focal terms sit on its diagonal in either storage order.
    if (state.CurrentMode() == Mode::GameCamera && state.PoseValid())
        PublishAimFor(state, g_entityHead, proj);
    return true;
}

// The camera-setup matrices are stored transposed relative to the constant-
// buffer copies (translation in the last ROW), so post-multiplying by X there is
// the same as PRE-multiplying by X^T here.
//
// Only `view` is rotated. The other two arguments are projections - they carry
// no view component, exactly like the bare P the buffer path leaves alone - and
// the engine builds the view-projection from view * proj itself, so rotating the
// view is enough for every derived matrix to come out consistent. The arguments
// belong to the caller, so they are copied before being modified: game logic
// must keep seeing the clean camera.
void __fastcall CameraSetupDetour(void* self, void* edx, void* renderer, float* view, float* proj,
                                  float* proj2) {
    float V[16], rotView[16], wideProj[16];
    float* viewArg = view;
    float* projArg = proj;

    InjectionState& state = State();
    if (RecognisePlayerCamera(view, proj, V) && state.CurrentMode() == Mode::CameraSetup) {
        // This hook is handed the clean view, so it is also where the
        // world-space yaw axis comes from in this mode.
        float upCam[3];
        WorldUpInCameraSpace(V, upCam);
        HeadTransform head;
        if (!state.BuildHeadTransform(upCam, head)) {
            // Nothing else publishes the aim in this mode, so leaving it alone
            // would strand the reticle and the interaction prompt at whatever
            // offset the last game-camera frame left them.
            state.ClearAim();
        } else {
            PublishAimFor(state, head, proj);

            float X[16], Xinv[16], Xt[16], camPos[3], camPosMoved[3];
            head.ConjugateForView(V, X, Xinv, camPos, camPosMoved);
            mat::Transpose4(X, Xt);

            mat::Mul4(Xt, view, rotView);
            mat::Copy16(rotView, g_lastRotatedView);
            g_haveLastRotatedView = true;
            viewArg = rotView;

            const float widen = state.FrustumWiden();
            if (widen < 0.999f) {
                mat::Copy16(proj, wideProj);
                wideProj[0] *= widen;
                wideProj[5] *= widen;
                projArg = wideProj;
            }

            if (!g_sourceLogged) {
                g_sourceLogged = true;
                logging::Line("camera: source injection active (camera setup hook)");
            }
        }
    }
    g_origCameraSetup(self, edx, renderer, viewArg, projArg, proj2);
}

// --- installation ----------------------------------------------------------

bool InstallHook(const char* what, uintptr_t rva, void* detour, void** original) {
    if (!CreateAndEnable(rva, detour, original)) {
        logging::Line("camera: %s hook FAILED", what);
        return false;
    }
    logging::Line("camera: %s hook installed at AI.exe+0x%X", what, static_cast<unsigned>(rva));
    return true;
}

}  // namespace

void Install() {
    // Pinned RVAs are only valid for the build they were derived from, so every
    // hook here stays dormant on anything else rather than corrupting a patched
    // executable. Constant-buffer injection needs no pinned address and carries
    // on alone.
    if (!builds::MatchesKnownBuild()) {
        logging::Line("camera: unknown AI.exe build; every pinned-address hook stays off and "
                      "only constant-buffer injection is available");
        State().SetMode(Mode::ConstantBuffers);
        return;
    }

    logging::Line("camera: build profile %s matched; installing pinned hooks",
                  builds::ActiveProfile().name);

    const builds::OffsetTable::Hooks& hooks = builds::ActiveOffsets().hooks;
    InstallHook("look-at", hooks.camera_inputs, &CameraInputsDetour,
                reinterpret_cast<void**>(&g_origCameraInputs));
    InstallHook("cached-forward", hooks.camera_derive, &CameraDeriveDetour,
                reinterpret_cast<void**>(&g_origCameraDerive));
    InstallFrameTaskHooks(g_renderTasks, kRenderDetours, hooks.render_tasks,
                          builds::kRenderTaskCount, "render-task");
    logging::Line("camera: render-window hooks installed; each logs once when it first runs");
    InstallFrameTaskHooks(g_cleanTasks, kCleanDetours, hooks.clean_tasks, builds::kCleanTaskCount,
                          "clean-task");
    // Without it there is no camera-setup mode to switch to, so the fallback is
    // the injection point that needs no pinned address at all.
    if (!InstallHook("camera setup", hooks.camera_setup, &CameraSetupDetour,
                     reinterpret_cast<void**>(&g_origCameraSetup)))
        State().SetMode(Mode::ConstantBuffers);
}

void ClearRotatedViewCache() { g_haveLastRotatedView = false; }

}  // namespace engine
}  // namespace camera
