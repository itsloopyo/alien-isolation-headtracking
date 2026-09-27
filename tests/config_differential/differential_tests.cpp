// The config differential test (convert-a-mod-to-the-canonical-config, section 5).
// Every input is read three ways:
//
//   oracle     the reader of the newest published build, the rolling `dev`
//              pre-release at e04ae0c, with the core sources it compiled at its
//              pin 3465659 (oracle_adapter.h)
//   import     the frozen reader in src/legacy_config/
//   migration  the config owner in a folder holding only
//              AlienIsolationHeadTracking.ini, the legacy file, importing it into
//              a new CameraUnlock.ini, then the canonical reader and table on it
//
// Comparison 1, oracle against import, on every input: every field both read
// (floats bit for bit), the startup state, and which actions every key press
// fires under every set of held modifiers. The differences it may find are
// kComparison1Differences below.
//
// Comparison 2, import against migration, is the proof for the migration: the
// settings the mod starts on and the actions every key press fires are the
// import's, apart from normalisations N1 and N3 on the yaw key, which the import
// must record as dropped. The old build had no setting for the port, the
// startup state, the tracking mode, the lean limits or the toggle and mode
// keys: it ran on core's defaults for them and bound End, Page Up and the
// chords in code.
//
// A setting the player never changed from what the published build shipped
// follows Defaults.ini: the import lists its row in follows_defaults_ini, the
// tracking mode pair as one unit, and the migration writes it `default`. The
// test derives that list from what the frozen reader read and holds the
// import's list to it on every input. Comparison 2 runs twice, once over a
// Defaults.ini at the built-in values and once over one a player changed on
// every row this game takes from it: a row left to Defaults.ini runs on that
// file's value, every other row on the import's. After every load the legacy
// file keeps its bytes, write time and attributes, Defaults.ini is never
// written, and the folder holds the legacy file and CameraUnlock.ini and
// nothing else. The next load reads CameraUnlock.ini, imports nothing and
// writes nothing, and a read-only legacy file imports as a writable one does.
// The distinct migrated files are written beside the executable under
// migrated/, for lint-migrated.mjs to run core's canonical config lint over.
//
// Inputs: no file, an empty file, the dev build's first-run output (no build
// shipped a config or a launcher seed), and core's corpus over that output.

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <array>
#include <functional>
#include <set>

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"
#include "config.h"
#include "legacy_config/legacy_config.h"
#include "oracle_adapter.h"

namespace fs = std::filesystem;
using namespace aiht;

namespace {

// dev always skipped the boot splash screens: its skip_frontend detour ran
// unconditionally. 2517d2d (fix: remove game footage from the repo and stop
// skipping the credits) put it behind [General] SkipIntroMovies, off by
// default, before the conversion, so the import reads a key dev did not and
// every input starts with the splash screens playing unless the file turns the
// skip on.
const char* const kComparison1Differences[] = {
    "[General] SkipIntroMovies: not read by dev (e04ae0c), which always skipped the splash "
    "screens; read since 2517d2d, default false",
};

constexpr const char* kFileName = "AlienIsolationHeadTracking.ini";

int g_failures = 0;
int g_checks = 0;

void Check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) {
        if (g_failures < 200) std::printf("  FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

bool SameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("could not write " + path.string());
}

using Listing = std::vector<std::pair<std::string, std::string>>;

Listing List(const fs::path& dir) {
    Listing l;
    for (const auto& e : fs::directory_iterator(dir)) {
        l.emplace_back(e.path().filename().string(), ReadBytes(e.path()));
    }
    std::sort(l.begin(), l.end());
    return l;
}

void SetReadOnly(const fs::path& path, bool readOnly) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("no attributes for " + path.string());
    const DWORD next = readOnly ? (attrs | FILE_ATTRIBUTE_READONLY) : (attrs & ~FILE_ATTRIBUTE_READONLY);
    if (!SetFileAttributesW(path.c_str(), next)) {
        throw std::runtime_error("could not set attributes on " + path.string());
    }
}

struct Input {
    std::string name;
    std::optional<std::string> bytes;  // nullopt: no file
};

// Startup state as dev's InitThread and camera::Install derive it: tracking on,
// rotation and position, the yaw mode from WorldSpaceYaw, the helmet from
// HelmetFollowsHead and both smoothing values. The intro skip is
// kComparison1Differences.
std::vector<std::string> FieldDifferences(const aiht_oracle_view::OracleSettings& o,
                                          const legacy::Settings& i) {
    std::vector<std::string> d;
    if (o.world_space_yaw != i.world_space_yaw) d.push_back("world_space_yaw");
    if (o.yaw_mode_key != i.yaw_mode_key) d.push_back("yaw_mode_key");
    if (o.helmet_follows_head != i.helmet_follows_head) d.push_back("helmet_follows_head");
    if (!SameBits(o.local_smoothing, i.local_smoothing)) d.push_back("local_smoothing");
    if (!SameBits(o.remote_smoothing, i.remote_smoothing)) d.push_back("remote_smoothing");
    return d;
}

std::string Join(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
}

std::string FirstFireDifference(const aiht_oracle_view::FireTable& expected,
                                const aiht_oracle_view::FireTable& got) {
    for (std::size_t i = 0; i < expected.size() && i < got.size(); ++i) {
        if (expected[i] != got[i]) {
            char text[96];
            std::snprintf(text, sizeof text, "key 0x%02X held %d",
                          static_cast<int>(i / aiht_oracle_view::kHeldStates) + aiht_oracle_view::kFirstKey,
                          static_cast<int>(i % aiht_oracle_view::kHeldStates));
            return text;
        }
    }
    return expected.size() == got.size() ? "none" : "the tables differ in size";
}

// The corpus descriptor of every key the frozen reader reads.
std::vector<cameraunlock::config::testing::MutationKey> MutationKeys() {
    using cameraunlock::config::testing::MutationKey;
    auto plain = [](const char* s, const char* k, const char* alt, std::vector<std::string> oor = {}) {
        MutationKey m;
        m.section = s;
        m.key = k;
        m.alternate = alt;
        m.out_of_range = std::move(oor);
        return m;
    };
    MutationKey yaw = plain("Hotkeys", "YawModeKey", "0x72", {"0x100", "0x10"});
    yaw.hotkey = true;
    return {
        plain("General", "WorldSpaceYaw", "false"),
        plain("General", "HelmetFollowsHead", "false"),
        plain("General", "SkipIntroMovies", "true"),
        plain("General", "LocalSmoothing", "0.3", {"-0.5", "1.5"}),
        plain("General", "RemoteSmoothing", "0.3", {"-0.5", "1.5"}),
        yaw,
    };
}

// A folder of this process's own, emptied before each input.
class Scratch {
public:
    Scratch() {
        root_ = fs::temp_directory_path() /
                ("aiht-config-differential-" + std::to_string(GetCurrentProcessId()));
        Remove(root_);
        fs::create_directories(root_);
    }
    ~Scratch() { Remove(root_); }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    fs::path Clean(const std::string& leaf) {
        const fs::path dir = root_ / leaf;
        Remove(dir);
        fs::create_directories(dir);
        return dir;
    }

private:
    // Read-only files included, which remove_all will not delete.
    static void Remove(const fs::path& dir) {
        std::error_code ec;
        if (!fs::exists(dir, ec)) return;
        for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
            if (e.is_regular_file()) SetFileAttributesW(e.path().c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        fs::remove_all(dir, ec);
        if (ec) throw std::runtime_error("could not empty " + dir.string() + ": " + ec.message());
    }

    fs::path root_;
};

fs::path Place(const fs::path& dir, const Input& input) {
    const fs::path file = dir / kFileName;
    if (input.bytes) WriteBytes(file, *input.bytes);
    return file;
}

// dev reads, and creates, the file beside the executable.
aiht_oracle_view::OracleSettings RunOracleOn(const Input& input) {
    const fs::path file = aiht_oracle_view::OraclePath();
    std::error_code ec;
    fs::remove(file, ec);
    if (input.bytes) WriteBytes(file, *input.bytes);
    const aiht_oracle_view::OracleSettings s = aiht_oracle_view::RunOracle();
    fs::remove(file, ec);
    return s;
}

struct ImportRun {
    legacy::Settings settings;
    legacy::ReadStatus status = legacy::ReadStatus::Read;
};

// The import on a read-only copy of the input, which must leave its folder as it
// found it.
ImportRun RunImport(Scratch& scratch, const Input& input) {
    const fs::path dir = scratch.Clean("import");
    const fs::path file = Place(dir, input);
    if (input.bytes) SetReadOnly(file, true);
    const Listing before = List(dir);
    ImportRun run;
    run.status = run.settings.Read(file.string());
    Check(List(dir) == before, input.name + ": the import changed its folder");
    return run;
}

ImportRun Comparison1(Scratch& scratch, const Input& input) {
    const aiht_oracle_view::OracleSettings oracle = RunOracleOn(input);
    const ImportRun import = RunImport(scratch, input);
    Check(input.bytes.has_value() == (import.status == legacy::ReadStatus::Read),
          input.name + ": the import's status does not match whether there is a file");
    const std::vector<std::string> fields = FieldDifferences(oracle, import.settings);
    Check(fields.empty(), input.name + ": fields differ: " + Join(fields));
    const aiht_oracle_view::FireTable oracleFires = aiht_oracle_view::OracleFires(oracle.yaw_mode_key);
    const aiht_oracle_view::FireTable importFires =
        aiht_oracle_view::OracleFires(import.settings.yaw_mode_key);
    Check(oracleFires == importFires,
          input.name + ": hotkeys fire differently: " + FirstFireDifference(oracleFires, importFires));
    return import;
}

// ---------------------------------------------------------------------------
// Comparison 2
// ---------------------------------------------------------------------------

namespace cfg = cameraunlock::config;
using cfg::ConfigLoadStatus;
using cfg::DropRule;
using cfg::DroppedValue;
using cfg::ImportResult;
using cfg::ImportStatus;
using cfg::schema::Concept;
using ConceptSet = std::set<Concept>;
using aiht_oracle_view::FireTable;
using aiht_oracle_view::kActions;

struct FileStamp {
    std::string bytes;
    FILETIME written{};
    DWORD attributes = 0;
    bool operator==(const FileStamp& o) const {
        return bytes == o.bytes && CompareFileTime(&written, &o.written) == 0 && attributes == o.attributes;
    }
};

FileStamp Stamp(const fs::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        throw std::runtime_error("cannot stat " + path.string());
    }
    return {ReadBytes(path), data.ftLastWriteTime, data.dwFileAttributes};
}

fs::path g_builtinDefaults;
fs::path g_alteredDefaults;
// What each Defaults.ini gives: the settings a fresh install starts on over it.
Config g_builtinConfig;
Config g_alteredConfig;

cfg::ConfigOwnerOptions<Config> OwnerOptions(const fs::path& dir, const fs::path& defaults) {
    return MakeConfigOwnerOptions(dir.wstring() + L"\\", cfg::DefaultsFile::At(defaults.wstring()));
}

ImportResult RunMappedImport(Scratch& scratch, const Input& input) {
    const fs::path file = Place(scratch.Clean("mapped"), input);
    Config out = MakeConfigTable().defaults();
    return MakeLegacyImport().run(cfg::LegacyInput{file.wstring(), file.string(), false}, out);
}

// The held modifiers CurrentFires presses each key under.
cameraunlock::input::KeyModifiers g_currentHeld = cameraunlock::input::KeyModifiers::kNone;
cameraunlock::input::KeyModifiers CurrentHeld() { return g_currentHeld; }

cameraunlock::input::KeyModifiers ModifiersOf(int held) {
    using cameraunlock::input::KeyModifiers;
    KeyModifiers m = KeyModifiers::kNone;
    if ((held & 1) != 0) m = m | KeyModifiers::kCtrl;
    if ((held & 2) != 0) m = m | KeyModifiers::kShift;
    if ((held & 4) != 0) m = m | KeyModifiers::kAlt;
    return m;
}

// OracleFires' table for the current build: RegisterKeyBindings puts one
// detail::GuardKey callback per distinct key of each list on the poller, and
// the same callbacks are built here with the held modifiers read from the test.
FireTable CurrentFires(const Config& m) {
    using aiht_oracle_view::kFirstKey;
    using aiht_oracle_view::kHeldStates;
    using aiht_oracle_view::kLastKey;
    std::array<int, kActions> fired{};
    std::vector<std::pair<int, std::function<void()>>> registered;
    const std::string* lists[kActions] = {&m.toggle_key_name, &m.cycle_tracking_mode_key_name, &m.yaw_mode_key_name,
                                          &m.frustum_widening_key_name, &m.injection_mode_key_name};
    for (int action = 0; action < kActions; ++action) {
        const cameraunlock::input::KeyBindingsParseResult parsed =
            cameraunlock::input::ParseKeyBindings(*lists[action]);
        if (!parsed.ok()) throw std::logic_error("migrated hotkey list '" + *lists[action] + "' does not parse");
        std::vector<int> keys;
        std::vector<std::vector<cameraunlock::input::KeyModifiers>> modifiers;
        for (const cameraunlock::input::KeyBinding& b : parsed.bindings) {
            const auto at = std::find(keys.begin(), keys.end(), b.vk);
            if (at == keys.end()) {
                keys.push_back(b.vk);
                modifiers.push_back({b.modifiers});
            } else {
                modifiers[static_cast<std::size_t>(at - keys.begin())].push_back(b.modifiers);
            }
        }
        for (std::size_t i = 0; i < keys.size(); ++i) {
            registered.emplace_back(keys[i], cameraunlock::input::detail::GuardKey(
                                                 std::move(modifiers[i]), [&fired, action] { ++fired[action]; },
                                                 &CurrentHeld));
        }
    }
    FireTable table;
    table.reserve((kLastKey - kFirstKey + 1) * kHeldStates);
    for (int vk = kFirstKey; vk <= kLastKey; ++vk) {
        for (int held = 0; held < kHeldStates; ++held) {
            fired = {};
            g_currentHeld = ModifiersOf(held);
            for (const auto& r : registered) {
                if (r.first == vk) r.second();
            }
            table.push_back(fired);
        }
    }
    g_currentHeld = cameraunlock::input::KeyModifiers::kNone;
    return table;
}

bool IsModifierKey(int vk) { return vk == 0x10 || vk == 0x11 || vk == 0x12 || (vk >= 0xA0 && vk <= 0xA5); }

// N1 and N3: a yaw key outside 0x01-0xFE, or on Ctrl, Shift or Alt alone,
// imports as unbound. 0 was already unbound.
bool YawKeyDropped(int vk) { return vk != 0 && (vk < 0x01 || vk > 0xFE || IsModifierKey(vk)); }

// The yaw key is dropped exactly where N1 or N3 applies, under its rule, and
// nothing else is dropped.
void CheckDrops(const std::string& name, const legacy::Settings& l, const ImportResult& imported) {
    Check(imported.pose_shaping.empty(), name + ": the import lists pose-shaping values the old build never read");
    std::size_t expected = 0;
    if (YawKeyDropped(l.yaw_mode_key)) {
        expected = 1;
        const DropRule rule = IsModifierKey(l.yaw_mode_key) ? DropRule::ModifierKey : DropRule::KeyCodeOutOfRange;
        bool found = false;
        for (const DroppedValue& d : imported.dropped) {
            found = found || (d.rule == rule && d.section == "Hotkeys" && d.key == "YawModeKey");
        }
        Check(found, name + ": the unbound yaw key is not recorded under its rule");
    }
    Check(imported.dropped.size() == expected,
          name + ": the import drops " + std::to_string(imported.dropped.size()) + " values, not " +
              std::to_string(expected));
}

// Every row of the table that follows Defaults.ini.
const ConceptSet& FollowingRows() {
    static const ConceptSet rows = {
        Concept::UdpPort,          Concept::EnableOnStartup,      Concept::WorldSpaceYaw,
        Concept::RotationEnabled,  Concept::PositionEnabled,      Concept::LocalSmoothing,
        Concept::RemoteSmoothing,  Concept::PositionLimitX,       Concept::PositionLimitY,
        Concept::PositionLimitYDown, Concept::PositionLimitZ,     Concept::PositionLimitZBack,
        Concept::ToggleKey,        Concept::CycleTrackingModeKey, Concept::YawModeKey,
    };
    return rows;
}

// The rows the player never changed: every row the old build had no setting
// for, and each it read that holds what it shipped.
ConceptSet UntouchedRows(const legacy::Settings& l) {
    const legacy::Settings s;
    ConceptSet untouched = FollowingRows();
    if (l.world_space_yaw != s.world_space_yaw) untouched.erase(Concept::WorldSpaceYaw);
    if (l.local_smoothing != s.local_smoothing) untouched.erase(Concept::LocalSmoothing);
    if (l.remote_smoothing != s.remote_smoothing) untouched.erase(Concept::RemoteSmoothing);
    if (l.yaw_mode_key != s.yaw_mode_key) untouched.erase(Concept::YawModeKey);
    return untouched;
}

std::string Names(const ConceptSet& rows) {
    std::string text;
    for (const Concept row : rows) {
        text += (text.empty() ? "" : ", ") + std::string(cfg::schema::kConcepts[static_cast<std::size_t>(row)].name);
    }
    return text.empty() ? "none" : text;
}

// The settings the mod starts on after the migration against the ones the
// frozen reader's build started on. A row in `follows` runs on `d`, what
// Defaults.ini gives, instead of the legacy value; for the settings the old
// build did not have, the legacy value is what it ran on, core's defaults.
std::vector<std::string> StartupDifferences(const legacy::Settings& l, const Config& m, const ConceptSet& follows,
                                            const Config& d) {
    const Config fixed;
    std::vector<std::string> diff;
    const auto from = [&follows](Concept row) { return follows.count(row) != 0; };
    const auto flag = [&](Concept row, bool got, bool legacyValue, bool defaultsValue, const char* name) {
        if (got != (from(row) ? defaultsValue : legacyValue)) diff.push_back(name);
    };
    const auto number = [&](Concept row, float got, float legacyValue, float defaultsValue, const char* name) {
        if (!SameBits(got, from(row) ? defaultsValue : legacyValue)) diff.push_back(name);
    };
    if (m.udp_port != (from(Concept::UdpPort) ? d.udp_port : fixed.udp_port)) diff.push_back("UdpPort");
    flag(Concept::EnableOnStartup, m.enable_on_startup, fixed.enable_on_startup, d.enable_on_startup,
         "EnableOnStartup");
    flag(Concept::WorldSpaceYaw, m.world_space_yaw, l.world_space_yaw, d.world_space_yaw, "WorldSpaceYaw");
    if (from(Concept::RotationEnabled) != from(Concept::PositionEnabled)) diff.push_back("half a tracking mode");
    const auto mode = cameraunlock::DecodeTrackingMode(m.rotation_enabled, m.position_enabled);
    const auto want = from(Concept::PositionEnabled)
                          ? cameraunlock::DecodeTrackingMode(d.rotation_enabled, d.position_enabled)
                          : std::optional<cameraunlock::TrackingMode>(cameraunlock::TrackingMode::RotationAndPosition);
    if (!mode || mode != want) diff.push_back("tracking mode");
    number(Concept::LocalSmoothing, m.local_smoothing, l.local_smoothing, d.local_smoothing, "LocalSmoothing");
    number(Concept::LocalSmoothing, m.position.local_smoothing, l.local_smoothing, d.position.local_smoothing,
           "LocalSmoothing (position)");
    number(Concept::RemoteSmoothing, m.remote_smoothing, l.remote_smoothing, d.remote_smoothing, "RemoteSmoothing");
    number(Concept::RemoteSmoothing, m.position.remote_smoothing, l.remote_smoothing, d.position.remote_smoothing,
           "RemoteSmoothing (position)");
    number(Concept::PositionLimitX, m.position.limit_x, fixed.position.limit_x, d.position.limit_x, "PositionLimitX");
    number(Concept::PositionLimitY, m.position.limit_y, fixed.position.limit_y, d.position.limit_y, "PositionLimitY");
    number(Concept::PositionLimitYDown, m.position.limit_y_down, fixed.position.limit_y_down,
           d.position.limit_y_down, "PositionLimitYDown");
    number(Concept::PositionLimitZ, m.position.limit_z, fixed.position.limit_z, d.position.limit_z, "PositionLimitZ");
    number(Concept::PositionLimitZBack, m.position.limit_z_back, fixed.position.limit_z_back,
           d.position.limit_z_back, "PositionLimitZBack");
    if (m.helmet_follows_head != l.helmet_follows_head) diff.push_back("HelmetFollowsHead");
    if (m.skip_intro_movies != l.skip_intro_movies) diff.push_back("SkipIntroMovies");

    // Each action fires as Defaults.ini binds it where its row follows that
    // file, and as the legacy build fired it everywhere else, with a yaw key N1
    // or N3 unbinds taken as no key.
    const FireTable legacyFires = aiht_oracle_view::OracleFires(YawKeyDropped(l.yaw_mode_key) ? 0 : l.yaw_mode_key);
    const FireTable defaultsFires = CurrentFires(d);
    const Concept keyRows[3] = {Concept::ToggleKey, Concept::CycleTrackingModeKey, Concept::YawModeKey};
    FireTable expected = legacyFires;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        for (int action = 0; action < 3; ++action) {
            if (from(keyRows[action])) expected[i][action] = defaultsFires[i][action];
        }
    }
    const FireTable got = CurrentFires(m);
    if (expected != got) diff.push_back("hotkeys: " + FirstFireDifference(expected, got));
    return diff;
}

std::string AllValues(const Config& c) { return cfg::RenderCanonical(MakeConfigTable(), c, {kConfigDisplayName}); }

bool Contains(const std::vector<std::string>& lines, const std::string& text) {
    for (const std::string& line : lines) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

struct Tally {
    std::string committed;
    std::set<std::string> migrated;
    int created = 0;
    int imported = 0;
    int with_default_rows = 0;
    int with_values = 0;
    int with_key_dropped = 0;
    int touched = 0;
};

void Comparison2(Scratch& scratch, const Input& input, const ImportRun& import, const ImportResult* mapped,
                 const fs::path& defaults, Tally& tally) {
    const bool builtin = defaults == g_builtinDefaults;
    const std::string name =
        input.name + (builtin ? " (Defaults.ini at the built-in values)" : " (Defaults.ini changed)");

    const fs::path dir = scratch.Clean("migration");
    const fs::path config = dir / kConfigFileName;
    const fs::path legacyFile = Place(dir, input);
    const FileStamp defaultsBefore = Stamp(defaults);
    FileStamp legacyBefore;
    if (input.bytes) legacyBefore = Stamp(legacyFile);

    const cfg::ConfigLoadResult<Config> loaded = cfg::ConfigOwner<Config>(OwnerOptions(dir, defaults)).Load();
    const Listing after = List(dir);
    Check(Stamp(defaults) == defaultsBefore, name + ": the load wrote Defaults.ini");

    if (!input.bytes) {
        ++tally.created;
        Check(loaded.status == ConfigLoadStatus::Created, name + ": no file is not Created");
        Check(after == Listing{{kConfigFileName, tally.committed}},
              name + ": the folder does not hold CameraUnlock.ini as config/CameraUnlock.ini and nothing else");
        const std::vector<std::string> d = StartupDifferences(import.settings, loaded.config, FollowingRows(),
                                                              builtin ? g_builtinConfig : g_alteredConfig);
        Check(d.empty(), name + ": comparison 2: " + Join(d));
        return;
    }
    Check(Stamp(legacyFile) == legacyBefore,
          name + ": AlienIsolationHeadTracking.ini did not keep its bytes, write time and attributes");

    const ConceptSet follows(mapped->follows_defaults_ini.begin(), mapped->follows_defaults_ini.end());
    if (builtin) {
        CheckDrops(name, import.settings, *mapped);
        if (!mapped->dropped.empty()) ++tally.with_key_dropped;
        Check(follows.size() == mapped->follows_defaults_ini.size(), name + ": follows_defaults_ini names a row twice");
        const ConceptSet untouched = UntouchedRows(import.settings);
        Check(follows == untouched, name + ": the import leaves " + Names(follows) +
                                        " to Defaults.ini, and the player never changed " + Names(untouched));
        if (untouched != FollowingRows()) ++tally.touched;
    }

    const std::vector<std::string> d =
        StartupDifferences(import.settings, loaded.config, follows, builtin ? g_builtinConfig : g_alteredConfig);
    Check(d.empty(), name + ": comparison 2: " + Join(d));

    ++tally.imported;
    Check(loaded.status == ConfigLoadStatus::Migrated,
          name + ": the migration is " + cfg::ConfigLoadStatusName(loaded.status) + ": " + loaded.reason);
    if (loaded.status != ConfigLoadStatus::Migrated) return;
    Check(after.size() == 2 && after[0].first == kLegacyConfigFileName && after[1].first == kConfigFileName &&
              after[0].second == *input.bytes,
          name + ": the folder does not hold AlienIsolationHeadTracking.ini and CameraUnlock.ini and nothing else");
    Check(Contains(loaded.log, "created from"), name + ": the log does not say where CameraUnlock.ini came from");
    const std::string migrated = ReadBytes(config);
    tally.migrated.insert(migrated);
    for (const Concept row : follows) {
        const std::string key = cfg::schema::kConcepts[static_cast<std::size_t>(row)].key;
        Check(migrated.find("\r\n" + key + "=default\r\n") != std::string::npos,
              name + ": " + key + ", which the player never changed, is not written default");
    }
    if (migrated.find("=default\r\n") != std::string::npos) ++tally.with_default_rows;
    if (migrated != tally.committed) ++tally.with_values;

    // The next launch reads CameraUnlock.ini over the same Defaults.ini, to the
    // same settings, does not import, and writes neither file.
    {
        const cfg::ConfigLoadResult<Config> reread = cfg::ConfigOwner<Config>(OwnerOptions(dir, defaults)).Load();
        Check(reread.status == ConfigLoadStatus::Canonical && reread.diagnostics.empty(),
              name + ": the next launch does not read CameraUnlock.ini cleanly");
        Check(AllValues(reread.config) == AllValues(loaded.config), name + ": the next launch runs on other settings");
        Check(!Contains(reread.log, "created from"), name + ": the next launch imports again");
        Check(Contains(reread.log, "is left as it was and is not read"),
              name + ": the next launch does not say AlienIsolationHeadTracking.ini is not read");
        Check(List(dir) == after && Stamp(legacyFile) == legacyBefore && Stamp(defaults) == defaultsBefore,
              name + ": the next launch changed a file");
    }

    if (builtin) {
        const fs::path roDir = scratch.Clean("read-only");
        const fs::path roLegacy = Place(roDir, input);
        SetReadOnly(roLegacy, true);
        const FileStamp roBefore = Stamp(roLegacy);
        const cfg::ConfigLoadResult<Config> fromReadOnly =
            cfg::ConfigOwner<Config>(OwnerOptions(roDir, defaults)).Load();
        Check(fromReadOnly.status == ConfigLoadStatus::Migrated &&
                  AllValues(fromReadOnly.config) == AllValues(loaded.config) &&
                  ReadBytes(roDir / kConfigFileName) == migrated,
              name + ": a read-only AlienIsolationHeadTracking.ini does not import as a writable one does");
        Check(Stamp(roLegacy) == roBefore && (roBefore.attributes & FILE_ATTRIBUTE_READONLY) != 0,
              name + ": a read-only AlienIsolationHeadTracking.ini did not keep its attribute, bytes and write time");
    }
}

// Defaults.ini as a player may have changed it: every value this game takes
// from it differs from the built-in one, the ones the old build read set to the
// corpus's alternate so a corpus input holding that alternate migrates as
// default.
void WriteAlteredDefaults() {
    std::string text = ReadBytes(g_builtinDefaults);
    const std::pair<const char*, const char*> changes[] = {
        {"UdpPort=4242", "UdpPort=4243"},
        {"EnableOnStartup=true", "EnableOnStartup=false"},
        {"WorldSpaceYaw=true", "WorldSpaceYaw=false"},
        {"PositionEnabled=true", "PositionEnabled=false"},
        {"LocalSmoothing=0.0", "LocalSmoothing=0.3"},
        {"RemoteSmoothing=0.15", "RemoteSmoothing=0.3"},
        {"PositionLimitX=0.3", "PositionLimitX=0.5"},
        {"PositionLimitY=0.2", "PositionLimitY=0.5"},
        {"PositionLimitYDown=0.2", "PositionLimitYDown=0.5"},
        {"PositionLimitZ=0.4", "PositionLimitZ=0.5"},
        {"PositionLimitZBack=0.1", "PositionLimitZBack=0.2"},
        {"ToggleKey=End, Ctrl+Shift+Y", "ToggleKey=F1, Ctrl+Shift+Y"},
        {"CycleTrackingModeKey=PageUp, Ctrl+Shift+G", "CycleTrackingModeKey=F2, Ctrl+Shift+G"},
        {"YawModeKey=PageDown, Ctrl+Shift+H", "YawModeKey=F3, Ctrl+Shift+H"},
    };
    for (const auto& [from, to] : changes) {
        const std::string line = std::string("\r\n") + from + "\r\n";
        const size_t at = text.find(line);
        if (at == std::string::npos) {
            throw std::runtime_error(std::string("the created Defaults.ini has no line ") + from);
        }
        text.replace(at + 2, std::strlen(from), to);
    }
    fs::create_directories(g_alteredDefaults.parent_path());
    WriteBytes(g_alteredDefaults, text);
}

std::vector<Input> Inputs(const std::string& firstRun) {
    using cameraunlock::config::testing::GenerateIniMutations;
    std::vector<Input> inputs;
    inputs.push_back({"no file", std::nullopt});
    inputs.push_back({"empty file", std::string()});
    inputs.push_back({"dev first-run output", firstRun});
    for (auto& m : GenerateIniMutations(firstRun, legacy::ReadKeys(), MutationKeys())) {
        inputs.push_back({"corpus: " + m.name, std::move(m.bytes)});
    }
    return inputs;
}

}  // namespace

// `--first-run <path>` writes what the oracle creates for a missing file to
// <path>, which is how data/dev-first-run.ini was extracted.
int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--first-run") == 0) {
        const fs::path file = aiht_oracle_view::OraclePath();
        std::error_code ec;
        fs::remove(file, ec);
        aiht_oracle_view::RunOracle();
        const std::string bytes = ReadBytes(file);
        fs::remove(file, ec);
        WriteBytes(argv[2], bytes);
        return bytes.empty() ? 1 : 0;
    }
    try {
        Scratch scratch;
        Tally tally;
        tally.committed = ReadBytes(fs::path(AIHT_COMMITTED_CONFIG));
        Check(!tally.committed.empty(), "config/CameraUnlock.ini is missing");

        // Each Defaults.ini sits outside the game folder, in a user folder of its
        // own whose parent exists, as the owner requires before it creates it.
        g_builtinDefaults = scratch.Clean("user-builtin") / "CameraUnlock" / "Defaults.ini";
        g_alteredDefaults = scratch.Clean("user-altered") / "CameraUnlock" / "Defaults.ini";
        fs::create_directories(g_builtinDefaults.parent_path());
        {
            const fs::path dir = scratch.Clean("first-load");
            Check(cfg::ConfigOwner<Config>(OwnerOptions(dir, g_builtinDefaults)).Load().status ==
                      ConfigLoadStatus::Created,
                  "the first load is not Created");
            Check(fs::exists(g_builtinDefaults), "the first load did not create Defaults.ini");
        }
        WriteAlteredDefaults();
        g_builtinConfig =
            cfg::ConfigOwner<Config>(OwnerOptions(scratch.Clean("builtin-fresh"), g_builtinDefaults)).Load().config;
        g_alteredConfig =
            cfg::ConfigOwner<Config>(OwnerOptions(scratch.Clean("altered-fresh"), g_alteredDefaults)).Load().config;

        // The dev build's first-run output, committed once as test data, is what
        // the oracle still writes for a missing file.
        const std::string firstRun = ReadBytes(fs::path(AIHT_DIFFERENTIAL_DATA) / "dev-first-run.ini");
        Check(!firstRun.empty(), "data/dev-first-run.ini is missing");
        {
            const fs::path file = aiht_oracle_view::OraclePath();
            std::error_code ec;
            fs::remove(file, ec);
            aiht_oracle_view::RunOracle();
            Check(ReadBytes(file) == firstRun, "the oracle's first-run output differs from data/dev-first-run.ini");
            fs::remove(file, ec);
        }

        // Fresh equals upgrade: over Defaults.ini at the built-in values, the dev
        // build's first-run output imports into the committed file, which is what
        // a fresh install creates.
        {
            const fs::path dir = scratch.Clean("fresh-equals-upgrade");
            WriteBytes(dir / kFileName, firstRun);
            Check(cfg::ConfigOwner<Config>(OwnerOptions(dir, g_builtinDefaults)).Load().status ==
                          ConfigLoadStatus::Migrated &&
                      ReadBytes(dir / kConfigFileName) == tally.committed,
                  "the dev first-run output does not import into the committed file");
        }

        const std::vector<Input> inputs = Inputs(firstRun);
        std::printf("%zu inputs\n", inputs.size());
        std::printf("comparison 1, the oracle (dev e04ae0c) against the import:\n");
        for (const char* d : kComparison1Differences) std::printf("  recorded difference: %s\n", d);
        for (const Input& input : inputs) {
            const ImportRun import = Comparison1(scratch, input);
            std::optional<ImportResult> mapped;
            if (input.bytes) {
                mapped = RunMappedImport(scratch, input);
                Check(mapped->status == ImportStatus::Imported, input.name + ": the mapped import is not Imported");
                if (input.name == "empty file" || input.name == "dev first-run output") {
                    Check(ConceptSet(mapped->follows_defaults_ini.begin(), mapped->follows_defaults_ini.end()) ==
                              FollowingRows(),
                          input.name + ": not every row follows Defaults.ini");
                }
            }
            for (const fs::path& defaults : {g_builtinDefaults, g_alteredDefaults}) {
                Comparison2(scratch, input, import, mapped ? &*mapped : nullptr, defaults, tally);
            }
        }

        std::printf("comparison 2, the import against the migration, %zu distinct files:\n", tally.migrated.size());
        std::printf("  %d created, %d imported (%d holding a default row, %d a value)\n", tally.created,
                    tally.imported, tally.with_default_rows, tally.with_values);
        std::printf("  %d with the yaw key unbound by N1 or N3\n", tally.with_key_dropped);
        std::printf("  %d with a row the player changed\n", tally.touched);
        Check(tally.with_default_rows > 0 && tally.with_values > 0, "no import writes both default and a value");
        Check(tally.with_key_dropped > 0, "no input unbinds the yaw key");
        Check(tally.touched > 0, "no input changes a row");
        Check(tally.migrated.count(tally.committed) == 1, "no input migrated to the committed file");

        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const fs::path lintDir = fs::path(exe).parent_path() / "migrated";
        fs::remove_all(lintDir);
        fs::create_directories(lintDir);
        int n = 0;
        for (const std::string& file : tally.migrated) WriteBytes(lintDir / (std::to_string(n++) + ".ini"), file);
    } catch (const std::exception& e) {
        std::printf("  FAIL: threw: %s\n", e.what());
        ++g_failures;
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
