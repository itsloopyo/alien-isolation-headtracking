#include "config.h"

#include <windows.h>

#include <cmath>
#include <string>

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/logging/file_log.h"

namespace config {
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

std::string IniPathNextToExe() {
    char exePath[MAX_PATH] = {};
    // See ExeDirFileW: a failed or truncated call leaves a buffer that
    // must not be read as a terminated string.
    const DWORD len = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return ".\\AlienIsolationHeadTracking.ini";
    const std::string p(exePath, len);
    const size_t slash = p.find_last_of("\\/");
    const std::string dir = (slash == std::string::npos) ? "." : p.substr(0, slash);
    return dir + "\\AlienIsolationHeadTracking.ini";
}

void WriteDefaults(const std::string& path, const Settings& defaults) {
    IniWriter writer;
    if (!writer.Open(path)) {
        logging::Line("config: could not create %s", path.c_str());
        return;
    }
    writer.WriteSection("General");
    writer.WriteComment("Yaw mode: true = horizon-locked yaw (default), false = camera-local");
    writer.WriteString("WorldSpaceYaw", defaults.world_space_yaw ? "true" : "false");
    writer.WriteComment("Keep the space suit helmet on your head instead of leaving it "
                        "facing where the body looks");
    writer.WriteString("HelmetFollowsHead", defaults.helmet_follows_head ? "true" : "false");
    writer.WriteComment("Skip the boot splash screens. These carry the developer and "
                        "publisher credits, so this is off unless you turn it on");
    writer.WriteString("SkipIntroMovies", defaults.skip_intro_movies ? "true" : "false");
    writer.WriteComment("Smoothing applied when the tracker runs on this machine (loopback). "
                        "0 = no smoothing, 1 = heavy");
    writer.WriteDouble("LocalSmoothing", defaults.local_smoothing);
    writer.WriteComment("Smoothing applied when the tracker is a remote device on the network. "
                        "0 = no smoothing, 1 = heavy");
    writer.WriteDouble("RemoteSmoothing", defaults.remote_smoothing);
    writer.WriteBlankLine();
    writer.WriteSection("Hotkeys");
    writer.WriteComment("Page Down - toggle world/local yaw");
    writer.WriteHex("YawModeKey", defaults.yaw_mode_key);
    logging::Line("config: wrote defaults to %s", path.c_str());
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

Settings Load() {
    Settings settings;
    const std::string path = IniPathNextToExe();

    IniReader reader;
    if (!reader.Open(path)) {
        WriteDefaults(path, settings);
        ReportSettings(settings);
        return settings;
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
    return settings;
}

}  // namespace

const Settings& Get() {
    static const Settings settings = Load();
    return settings;
}

}  // namespace config
