#pragma once

// The oracle: the config reader and hotkey registration of the newest published
// build, the rolling `dev` pre-release at e04ae0c (core pin 3465659). The reader
// is oracle/src/config.cpp, compiled whole into oracle_config_adapter.cpp so its
// file-local Load can be called once per input. The hotkeys are the ten
// registrations dev's InitThread made, run through core's NavGuarded and
// ChordGuarded (unchanged since 3465659) with the held modifiers faked. This
// header names no core type.

#include <array>
#include <string>
#include <vector>

namespace aiht_oracle_view {

struct OracleSettings {
    bool world_space_yaw;
    int yaw_mode_key;
    bool helmet_follows_head;
    float local_smoothing;
    float remote_smoothing;
};

// The published Load on the file it reads, AlienIsolationHeadTracking.ini
// beside this executable, which it creates when there is none.
OracleSettings RunOracle();

// The file RunOracle reads and creates.
std::string OraclePath();

// Which actions a key press fires, for every key a binding can name (0x01-0xFE)
// under every set of held modifiers. Entry (vk - kFirstKey) * kHeldStates + held
// counts toggle, cycle tracking mode, yaw mode, frustum widening and injection
// mode, in that order. held: 1 Ctrl, 2 Shift, 4 Alt.
constexpr int kFirstKey = 0x01;
constexpr int kLastKey = 0xFE;
constexpr int kHeldStates = 8;
constexpr int kActions = 5;
using FireTable = std::vector<std::array<int, kActions>>;

FireTable OracleFires(int yaw_mode_key);

}  // namespace aiht_oracle_view
