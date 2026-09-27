#pragma once

// The reader every build before the canonical format ran on
// AlienIsolationHeadTracking.ini, frozen so a player updating from any of them
// has the file read exactly as that build read it. Nothing in this folder is
// ever edited. Three things differ from the reader it was taken from: it fills
// this frozen copy of that build's Settings and defaults rather than the
// runtime type, it takes the path instead of building it, and it never writes
// the file (a missing file reads as the defaults, which is what the old reader
// then read from the file it created there).

#include <string>
#include <vector>

#include "cameraunlock/config/legacy_import.h"

namespace aiht::legacy {

enum class ReadStatus {
    Read,
    // No file at the path. Settings holds the defaults.
    Absent,
};

struct Settings {
    bool world_space_yaw = true;
    int yaw_mode_key = 0x22;  // Page Down
    bool helmet_follows_head = true;
    bool skip_intro_movies = false;
    float local_smoothing = static_cast<float>(0.0);
    float remote_smoothing = static_cast<float>(0.15);

    // Call on a default-constructed Settings. `path` is the ANSI path the old
    // build opened.
    ReadStatus Read(const std::string& path);
};

// Every section and key Read reads, in the order it reads them.
std::vector<cameraunlock::config::LegacyKey> ReadKeys();

}  // namespace aiht::legacy
