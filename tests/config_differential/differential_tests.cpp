// The config differential test (convert-a-mod-to-the-canonical-config, section 5).
// Every input is read two ways:
//
//   oracle  the reader of the newest published build, the rolling `dev`
//           pre-release at e04ae0c, with the core sources it compiled at its
//           pin 3465659 (oracle_adapter.h)
//   import  the frozen reader in src/AlienIsolationHeadTracking/legacy_config/
//
// Comparison 1, oracle against import, on every input: every field both read
// (floats bit for bit), the startup state, and which actions every key press
// fires under every set of held modifiers. The differences it may find are
// kComparison1Differences below.
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

#include "cameraunlock/config/testing/ini_mutations.h"
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

        const std::vector<Input> inputs = Inputs(firstRun);
        std::printf("%zu inputs\n", inputs.size());
        std::printf("comparison 1, the oracle (dev e04ae0c) against the import:\n");
        for (const char* d : kComparison1Differences) std::printf("  recorded difference: %s\n", d);
        for (const Input& input : inputs) Comparison1(scratch, input);
    } catch (const std::exception& e) {
        std::printf("  FAIL: threw: %s\n", e.what());
        ++g_failures;
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
