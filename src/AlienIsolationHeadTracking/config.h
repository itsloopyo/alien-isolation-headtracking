#pragma once

namespace config {

// Settings read from AlienIsolationHeadTracking.ini beside AI.exe.
struct Settings {
    // Yaw about the world up axis rather than the camera's own, so looking at
    // the floor and turning the head pans across it instead of spinning the
    // view about the direction of gaze.
    bool world_space_yaw = true;
    int yaw_mode_key = 0x22;  // Page Down
};

// Reads the INI on first call, writing a commented default file when none is
// there. An entry the file does not carry keeps its default, so a file written
// by an older build still loads.
const Settings& Get();

}  // namespace config
