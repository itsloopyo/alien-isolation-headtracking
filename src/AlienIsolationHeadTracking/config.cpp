#include "config.h"

#include <windows.h>

#include <string>

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/logging/file_log.h"

namespace config {
namespace {

using namespace cameraunlock;

std::string IniPathNextToExe() {
    char exePath[MAX_PATH] = {};
    // See LogPathNextToExe: a failed or truncated call leaves a buffer that
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
    writer.WriteBlankLine();
    writer.WriteSection("Hotkeys");
    writer.WriteComment("Page Down - toggle world/local yaw");
    writer.WriteHex("YawModeKey", defaults.yaw_mode_key);
    logging::Line("config: wrote defaults to %s", path.c_str());
}

Settings Load() {
    Settings settings;
    const std::string path = IniPathNextToExe();

    IniReader reader;
    if (!reader.Open(path)) {
        WriteDefaults(path, settings);
        return settings;
    }

    settings.world_space_yaw =
        reader.ReadBool("General", "WorldSpaceYaw", settings.world_space_yaw);
    settings.helmet_follows_head =
        reader.ReadBool("General", "HelmetFollowsHead", settings.helmet_follows_head);
    settings.yaw_mode_key = reader.ReadHex("Hotkeys", "YawModeKey", settings.yaw_mode_key);
    logging::Line("config: WorldSpaceYaw=%s HelmetFollowsHead=%s YawModeKey=0x%02X",
                  settings.world_space_yaw ? "true" : "false",
                  settings.helmet_follows_head ? "true" : "false", settings.yaw_mode_key);
    return settings;
}

}  // namespace

const Settings& Get() {
    static const Settings settings = Load();
    return settings;
}

}  // namespace config
