#include "legacy_config/legacy_config.h"

#include <cmath>
#include <string>

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/logging/file_log.h"

namespace aiht::legacy {
namespace {

using namespace cameraunlock;

// Smoothing reaches cameraunlock::math::CalculateSmoothingFactor, which runs
// exp() on it. strtod parses "nan" and "inf" without complaint, and every
// comparison-based clamp downstream is skipped by NaN because each comparison
// against it is false, so "LocalSmoothing=nan" would otherwise poison the
// smoothed pose for the rest of the session with nothing in the log. Reject it
// here instead. This is validation and never a floor: a configured 0.0 comes
// back as 0.0.
float SanitizeSmoothing(const char* key, float value, float fallback) {
    if (!std::isfinite(value)) {
        logging::Line("config: %s is not a finite number, using %.2f", key, fallback);
        return fallback;
    }
    if (value < 0.0f || value > 1.0f) {
        const float clamped = (value < 0.0f) ? 0.0f : 1.0f;
        logging::Line("config: %s=%g is outside [0,1], clamped to %.2f", key,
                      static_cast<double>(value), clamped);
        return clamped;
    }
    return value;
}

// Logged on both paths, so a report from a first launch (which has no ini yet)
// still says what the mod is running with.
void ReportSettings(const Settings& settings) {
    logging::Line("config: WorldSpaceYaw=%s HelmetFollowsHead=%s SkipIntroMovies=%s "
                  "LocalSmoothing=%.2f RemoteSmoothing=%.2f YawModeKey=0x%02X",
                  settings.world_space_yaw ? "true" : "false",
                  settings.helmet_follows_head ? "true" : "false",
                  settings.skip_intro_movies ? "true" : "false", settings.local_smoothing,
                  settings.remote_smoothing, settings.yaw_mode_key);
}

}  // namespace

ReadStatus Settings::Read(const std::string& path) {
    Settings& settings = *this;

    IniReader reader;
    if (!reader.Open(path)) {
        ReportSettings(settings);
        return ReadStatus::Absent;
    }

    settings.world_space_yaw =
        reader.ReadBool("General", "WorldSpaceYaw", settings.world_space_yaw);
    settings.helmet_follows_head =
        reader.ReadBool("General", "HelmetFollowsHead", settings.helmet_follows_head);
    settings.skip_intro_movies =
        reader.ReadBool("General", "SkipIntroMovies", settings.skip_intro_movies);
    // Each key falls back to its own default (local 0.0, remote 0.15), not to a
    // shared one: a bad RemoteSmoothing dropping to the local default would
    // leave a phone's network jitter entirely unsmoothed.
    const float local_default = settings.local_smoothing;
    const float remote_default = settings.remote_smoothing;
    settings.local_smoothing = SanitizeSmoothing(
        "LocalSmoothing", reader.ReadFloat("General", "LocalSmoothing", local_default),
        local_default);
    settings.remote_smoothing = SanitizeSmoothing(
        "RemoteSmoothing", reader.ReadFloat("General", "RemoteSmoothing", remote_default),
        remote_default);
    settings.yaw_mode_key = reader.ReadHex("Hotkeys", "YawModeKey", settings.yaw_mode_key);
    ReportSettings(settings);
    return ReadStatus::Read;
}

std::vector<cameraunlock::config::LegacyKey> ReadKeys() {
    return {
        {"General", "WorldSpaceYaw"},
        {"General", "HelmetFollowsHead"},
        {"General", "SkipIntroMovies"},
        {"General", "LocalSmoothing"},
        {"General", "RemoteSmoothing"},
        {"Hotkeys", "YawModeKey"},
    };
}

}  // namespace aiht::legacy
