#pragma once

#include "cameraunlock/math/smoothing_utils.h"

namespace config {

// Settings read from AlienIsolationHeadTracking.ini beside AI.exe.
struct Settings {
    // Yaw about the world up axis rather than the camera's own, so looking at
    // the floor and turning the head pans across it instead of spinning the
    // view about the direction of gaze.
    bool world_space_yaw = true;
    int yaw_mode_key = 0x22;  // Page Down

    // Keeps the space suit helmet on the player's head. The helmet is geometry
    // attached to the camera and positioned by the engine's entity update, so it
    // only follows the head if the camera is still rotated when that update
    // runs. Off, the helmet stays where the body is looking: turn far enough and
    // you are looking at the back of a shell that was never modelled.
    bool helmet_follows_head = true;

    // Skips the boot splash sequence by answering yes to the game's own
    // skip_frontend flag. Off by default and deliberately opt-in: those splash
    // screens carry the developer, publisher and rights-holder credits, and
    // suppressing them is the player's call to make, not a side effect of
    // installing head tracking.
    bool skip_intro_movies = false;

    // Smoothing is picked per connection from the packet source address: a
    // tracker running on this machine (loopback) uses local_smoothing, a phone
    // or other device on the network uses remote_smoothing. Both cover rotation
    // and position. 0 = none, 1 = heavy.
    float local_smoothing = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);
};

// Reads the INI on first call, writing a commented default file when none is
// there. An entry the file does not carry keeps its default, so a file written
// by an older build still loads.
const Settings& Get();

}  // namespace config
