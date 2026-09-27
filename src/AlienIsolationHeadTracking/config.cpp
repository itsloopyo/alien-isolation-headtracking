#include "config.h"

#include <windows.h>

#include <string>

#include "cameraunlock/config/ini_reader.h"
#include "legacy_config/legacy_config.h"
#include "cameraunlock/logging/file_log.h"

namespace config {
namespace {

using namespace cameraunlock;

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

Settings Load() {
    const Settings defaults;
    const std::string path = IniPathNextToExe();

    aiht::legacy::Settings read;
    if (read.Read(path) == aiht::legacy::ReadStatus::Absent) {
        WriteDefaults(path, defaults);
    }

    Settings settings;
    settings.world_space_yaw = read.world_space_yaw;
    settings.yaw_mode_key = read.yaw_mode_key;
    settings.helmet_follows_head = read.helmet_follows_head;
    settings.skip_intro_movies = read.skip_intro_movies;
    settings.local_smoothing = read.local_smoothing;
    settings.remote_smoothing = read.remote_smoothing;
    return settings;
}

}  // namespace

const Settings& Get() {
    static const Settings settings = Load();
    return settings;
}

}  // namespace config
