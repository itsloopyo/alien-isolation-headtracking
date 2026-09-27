#include "config.h"

#include <string>
#include <utility>
#include <vector>

#include "cameraunlock/config/head_tracking_config_table.h"
#include "legacy_config/legacy_config.h"

namespace aiht {
namespace {

using cameraunlock::config::DroppedValue;
using cameraunlock::config::ImportResult;
using cameraunlock::config::LegacyInput;
using cameraunlock::config::schema::Concept;

// The builds before the canonical format bound End, Page Up, Insert and Delete
// and every Ctrl+Shift chord in code; only the yaw key was read from the file.
constexpr const char* kLegacyToggleKeys = "End, Ctrl+Shift+Y";
constexpr const char* kLegacyCycleModeKeys = "PageUp, Ctrl+Shift+G";
constexpr const char* kLegacyYawChord = "Ctrl+Shift+H";

ImportResult Import(const LegacyInput& input, Config& out) {
    // The published build opened the file by the ANSI path of its own folder,
    // so the import reads it by the same form.
    legacy::Settings c;
    const legacy::ReadStatus read = c.Read(input.ansi_path);

    std::vector<DroppedValue> dropped;

    out.world_space_yaw = c.world_space_yaw;
    out.helmet_follows_head = c.helmet_follows_head;
    out.skip_intro_movies = c.skip_intro_movies;
    out.local_smoothing = c.local_smoothing;
    out.position.local_smoothing = c.local_smoothing;
    out.remote_smoothing = c.remote_smoothing;
    out.position.remote_smoothing = c.remote_smoothing;

    out.toggle_key_name = kLegacyToggleKeys;
    out.cycle_tracking_mode_key_name = kLegacyCycleModeKeys;
    const std::string yawKey =
        cameraunlock::config::LegacyVirtualKeyToBindings(c.yaw_mode_key, "Hotkeys", "YawModeKey", dropped);
    out.yaw_mode_key_name = yawKey.empty() ? std::string(kLegacyYawChord) : yawKey + ", " + kLegacyYawChord;

    // A setting the player never changed from what the published build shipped
    // follows Defaults.ini. The old build had no setting for the port, the
    // startup state, the tracking mode, the lean limits or the toggle and mode
    // keys, so no player changed those.
    const legacy::Settings shipped;
    cameraunlock::config::LegacyFollowsDefaultsIni follows;
    follows.NotInLegacy(Concept::UdpPort);
    follows.NotInLegacy(Concept::EnableOnStartup);
    follows.Setting(Concept::WorldSpaceYaw, c.world_space_yaw, shipped.world_space_yaw);
    follows.TrackingMode(true);
    follows.Setting(Concept::LocalSmoothing, c.local_smoothing, shipped.local_smoothing);
    follows.Setting(Concept::RemoteSmoothing, c.remote_smoothing, shipped.remote_smoothing);
    follows.NotInLegacy(Concept::PositionLimitX);
    follows.NotInLegacy(Concept::PositionLimitY);
    follows.NotInLegacy(Concept::PositionLimitYDown);
    follows.NotInLegacy(Concept::PositionLimitZ);
    follows.NotInLegacy(Concept::PositionLimitZBack);
    follows.NotInLegacy(Concept::ToggleKey);
    follows.NotInLegacy(Concept::CycleTrackingModeKey);
    follows.Setting(Concept::YawModeKey, c.yaw_mode_key == shipped.yaw_mode_key);

    return read == legacy::ReadStatus::Absent
               ? ImportResult::Absent(std::move(dropped), {}, follows.Concepts())
               : ImportResult::Imported(std::move(dropped), {}, follows.Concepts());
}

Config g_settings;

}  // namespace

cameraunlock::config::ConfigTable<Config> MakeConfigTable() {
    using cameraunlock::config::BoolCodec;
    using cameraunlock::config::HotkeyCodec;
    cameraunlock::config::ConfigTable<Config> table = cameraunlock::config::HeadTrackingConfigTable<Config>(
        {Concept::UdpPort, Concept::EnableOnStartup, Concept::WorldSpaceYaw, Concept::RotationEnabled,
         Concept::LocalSmoothing, Concept::RemoteSmoothing, Concept::PositionEnabled, Concept::PositionLimitX,
         Concept::PositionLimitY, Concept::PositionLimitYDown, Concept::PositionLimitZ,
         Concept::PositionLimitZBack, Concept::ToggleKey, Concept::CycleTrackingModeKey, Concept::YawModeKey});
    table.Select(Concept::WorldSpaceYaw).Writable()
        .Select(Concept::RotationEnabled).Writable()
        .Select(Concept::PositionEnabled).Writable();
    table.Local("General", "HelmetFollowsHead", &Config::helmet_follows_head, BoolCodec(),
                "true: the space suit helmet stays on your head as you look around.\n"
                "false: it stays facing where your body looks.");
    table.Local("General", "SkipIntroMovies", &Config::skip_intro_movies, BoolCodec(),
                "true: skip the splash screens at startup. They carry the developer and\n"
                "publisher credits, so this is off unless you turn it on.");
    table.Local("Hotkeys", "FrustumWideningKey", &Config::frustum_widening_key_name, HotkeyCodec(),
                "Diagnostic: widens the view the game culls against, for this session only.\n"
                "The picture zooms out while it is on.");
    table.Local("Hotkeys", "InjectionModeKey", &Config::injection_mode_key_name, HotkeyCodec(),
                "Diagnostic: cycles where the head pose is applied, for this session only.");
    return table;
}

cameraunlock::config::LegacyImport<Config> MakeLegacyImport() { return {&Import, legacy::ReadKeys()}; }

cameraunlock::config::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(
    const std::wstring& folder, cameraunlock::config::DefaultsFile defaults) {
    const auto wide = [](const char* name) {
        return std::wstring(name, name + std::char_traits<char>::length(name));
    };
    cameraunlock::config::ConfigOwnerOptions<Config> options;
    options.path = folder + wide(kConfigFileName);
    options.legacy_path = folder + wide(kLegacyConfigFileName);
    options.table = MakeConfigTable();
    options.import = MakeLegacyImport();
    options.header.display_name = kConfigDisplayName;
    options.defaults = std::move(defaults);
    return options;
}

const Config& Settings() { return g_settings; }

void PublishSettings(const Config& config) { g_settings = config; }

}  // namespace aiht
