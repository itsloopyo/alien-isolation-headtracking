// dev's hotkey registrations (src/AlienIsolationHeadTracking/dllmain.cpp at
// e04ae0c, InitThread), run through the guards it compiled. The guards are
// core's chord_hotkeys.h, included inside a namespace of this file's own with
// GetAsyncKeyState pointed at the test's held modifiers, so its inline
// functions are not the ones the current build links.
#include <windows.h>

#include <functional>
#include <utility>
#include <vector>

#include "oracle_adapter.h"

namespace {
int g_held = 0;

SHORT FakeGetAsyncKeyState(int vk) {
    const bool down = (vk == VK_CONTROL && (g_held & 1) != 0) ||
                      (vk == VK_SHIFT && (g_held & 2) != 0) || (vk == VK_MENU && (g_held & 4) != 0);
    return down ? static_cast<SHORT>(0x8000) : 0;
}
}  // namespace

#define GetAsyncKeyState FakeGetAsyncKeyState
namespace aiht_oracle_guards {
#include "cameraunlock/input/chord_hotkeys.h"
}
#undef GetAsyncKeyState

namespace aiht_oracle_view {

FireTable OracleFires(int yaw_mode_key) {
    namespace input = aiht_oracle_guards::cameraunlock::input;
    std::array<int, kActions> fired{};
    auto action = [&fired](int i) { return [&fired, i] { ++fired[i]; }; };

    constexpr int kVkToggle = 0x23;
    constexpr int kVkTrackingMode = 0x21;
    constexpr int kVkFrustum = 0x2D;
    constexpr int kVkInjectMode = 0x2E;
    const std::vector<std::pair<int, std::function<void()>>> registered = {
        {kVkToggle, input::NavGuarded(action(0))},
        {kVkTrackingMode, input::NavGuarded(action(1))},
        {yaw_mode_key, input::NavGuarded(action(2))},
        {kVkFrustum, input::NavGuarded(action(3))},
        {kVkInjectMode, input::NavGuarded(action(4))},
        {'Y', input::ChordGuarded(action(0))},
        {'G', input::ChordGuarded(action(1))},
        {'H', input::ChordGuarded(action(2))},
        {'U', input::ChordGuarded(action(3))},
        {'J', input::ChordGuarded(action(4))},
    };

    // dev's poller runs a callback when its key goes down.
    FireTable table;
    table.reserve((kLastKey - kFirstKey + 1) * kHeldStates);
    for (int vk = kFirstKey; vk <= kLastKey; ++vk) {
        for (int held = 0; held < kHeldStates; ++held) {
            fired = {};
            g_held = held;
            for (const auto& r : registered) {
                if (r.first == vk) r.second();
            }
            table.push_back(fired);
        }
    }
    g_held = 0;
    return table;
}

}  // namespace aiht_oracle_view
