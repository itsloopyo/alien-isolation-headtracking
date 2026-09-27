#pragma once

#include <string>

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/config_table.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/head_tracking_config.h"
#include "cameraunlock/config/legacy_import.h"

namespace aiht {

constexpr const char* kConfigFileName = "CameraUnlock.ini";
// The file every build before the canonical format read, beside
// kConfigFileName. Imported once while kConfigFileName is absent, and never
// written.
constexpr const char* kLegacyConfigFileName = "AlienIsolationHeadTracking.ini";
// The game's name as cameraunlock-core's data/games.json spells it.
constexpr const char* kConfigDisplayName = "Alien: Isolation";

// Core's config with this game's own settings.
struct Config : cameraunlock::HeadTrackingConfig {
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

    // Two diagnostic toggles for this session: widening the frustum handed to
    // the engine, and cycling where the head pose is injected.
    std::string frustum_widening_key_name = "Insert, Ctrl+Shift+U";
    std::string injection_mode_key_name = "Delete, Ctrl+Shift+J";
};

// The rows of CameraUnlock.ini. Only the tracking mode pair and WorldSpaceYaw
// are Writable: the mode and yaw hotkeys save the player's choice, and End and
// the two diagnostic toggles change the session only.
cameraunlock::config::ConfigTable<Config> MakeConfigTable();

// AlienIsolationHeadTracking.ini as the builds before the canonical format read
// it (legacy_config/), mapped into Config.
cameraunlock::config::LegacyImport<Config> MakeLegacyImport();

// The owner's options for the files in `folder` (with its trailing separator):
// the settings in CameraUnlock.ini, imported once from
// AlienIsolationHeadTracking.ini. The mod passes DefaultsFile::PerUser() and a
// test a scratch file.
cameraunlock::config::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(
    const std::wstring& folder, cameraunlock::config::DefaultsFile defaults);

// The settings this session started on, which the init thread publishes once
// before anything reads them.
const Config& Settings();
void PublishSettings(const Config& config);

}  // namespace aiht
