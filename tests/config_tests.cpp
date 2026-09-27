// The committed config and the saves the hotkeys make.
//
// `config_tests --render-config <path>` writes the table's fresh render to
// <path> and runs nothing else; `pixi run render-config` uses it to rewrite
// config/CameraUnlock.ini.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/input/key_bindings.h"
#include "config.h"

namespace fs = std::filesystem;
namespace cfg = cameraunlock::config;
using namespace aiht;

namespace {

int g_failures = 0;

void Check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failures;
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("could not write " + path.string());
}

std::string Render() { return cfg::RenderCanonicalFresh(MakeConfigTable(), {kConfigDisplayName}); }

// Each line of `after` that is not the same line of `before`, both split on CRLF.
std::string ChangedLines(const std::string& before, const std::string& after) {
    auto split = [](const std::string& s) {
        std::vector<std::string> lines;
        std::size_t start = 0;
        for (std::size_t at; (at = s.find("\r\n", start)) != std::string::npos; start = at + 2) {
            lines.push_back(s.substr(start, at - start));
        }
        lines.push_back(s.substr(start));
        return lines;
    };
    const std::vector<std::string> a = split(before), b = split(after);
    if (a.size() != b.size()) return "<line count changed>";
    std::string changed;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) changed += (changed.empty() ? "" : " | ") + b[i];
    }
    return changed;
}

void RenderTest() {
    std::printf("committed config\n");
    Check(ReadBytes(AIHT_COMMITTED_CONFIG) == Render(),
          "config/CameraUnlock.ini is the table's fresh render (pixi run render-config)");
}

void HotkeyTest() {
    std::printf("hotkeys\n");
    const Config c = MakeConfigTable().defaults();
    const std::string lists[] = {c.toggle_key_name, c.cycle_tracking_mode_key_name, c.yaw_mode_key_name,
                                 c.frustum_widening_key_name, c.injection_mode_key_name};
    for (const std::string& list : lists) {
        Check(cameraunlock::input::ParseKeyBindings(list).ok(), "'" + list + "' parses");
    }
    Check(c.toggle_key_name == "End, Ctrl+Shift+Y", "toggle defaults to End, Ctrl+Shift+Y");
    Check(c.cycle_tracking_mode_key_name == "PageUp, Ctrl+Shift+G", "tracking mode defaults to PageUp, Ctrl+Shift+G");
    Check(c.yaw_mode_key_name == "PageDown, Ctrl+Shift+H", "yaw mode defaults to PageDown, Ctrl+Shift+H");
}

// A save changes the lines of its rows and no other byte, and one starting from
// a default row writes its value.
void SaveTest() {
    std::printf("saves\n");
    const fs::path root = fs::temp_directory_path() / ("aiht-config-tests-" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path dir = root / "game";
    fs::create_directories(dir);
    fs::create_directories(root / "user" / "CameraUnlock");
    const fs::path defaults = root / "user" / "CameraUnlock" / "Defaults.ini";
    auto options = [&] {
        return MakeConfigOwnerOptions(dir.wstring() + L"\\", cfg::DefaultsFile::At(defaults.wstring()));
    };

    cfg::ConfigOwner<Config> owner(options());
    Check(owner.Load().status == cfg::ConfigLoadStatus::Created, "a first start creates CameraUnlock.ini");
    const fs::path file = dir / kConfigFileName;
    const std::string fresh = ReadBytes(file);
    Check(fresh == Render(), "the created file is the committed render");
    const std::string defaultsBefore = ReadBytes(defaults);

    Check(owner.Save([](Config& c) { c.world_space_yaw = false; }).status == cfg::ConfigSaveStatus::Saved,
          "the yaw toggle saves");
    const std::string afterYaw = ReadBytes(file);
    Check(ChangedLines(fresh, afterYaw) == "WorldSpaceYaw=false", "the yaw save changes only WorldSpaceYaw");

    Check(owner.Save([](Config& c) {
                   c.rotation_enabled = true;
                   c.position_enabled = false;
               }).status == cfg::ConfigSaveStatus::Saved,
          "the tracking mode cycle saves");
    Check(ChangedLines(afterYaw, ReadBytes(file)) == "RotationEnabled=true | PositionEnabled=false",
          "the mode save changes only the pair");
    Check(ReadBytes(defaults) == defaultsBefore, "no save writes Defaults.ini");

    bool refused = false;
    try {
        owner.Save([](Config& c) { c.enable_on_startup = false; });
    } catch (const std::exception&) {
        refused = true;
    }
    Check(refused, "EnableOnStartup is not Writable, so End cannot persist");

    const cfg::ConfigLoadResult<Config> reread = cfg::ConfigOwner<Config>(options()).Load();
    Check(reread.status == cfg::ConfigLoadStatus::Canonical && !reread.config.world_space_yaw &&
              reread.config.rotation_enabled && !reread.config.position_enabled,
          "the next start reads both saves back");
    fs::remove_all(root, ec);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--render-config") == 0) {
        WriteBytes(argv[2], Render());
        return 0;
    }
    try {
        RenderTest();
        HotkeyTest();
        SaveTest();
    } catch (const std::exception& e) {
        Check(false, std::string("threw: ") + e.what());
    }
    std::printf("%d failures\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
