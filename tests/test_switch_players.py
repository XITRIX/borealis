#!/usr/bin/env python3
"""Check that Switch players keep their controllers across hotplug."""

import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

ADAPTER = r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#define GAMEPADS_MAX 5
using u32 = uint32_t;
struct PadState {
    u32 style_set = 0;
    bool active_handheld = false;
};
inline void padUpdate(PadState*) {}
namespace fmt {
template <class... Args> std::string format(const char*, Args...) { return {}; }
}
namespace brls {
struct Logger {
    template <class... Args> static void info(const char*, Args...) {}
};
struct SwitchInputManager {
    short getControllersConnectedCount();
    unsigned getControllersConnectedMask();
    PadState padStateHandheld;
    PadState padsState[GAMEPADS_MAX];
    static constexpr int HANDHELD_SOURCE = GAMEPADS_MAX;
    static constexpr int NO_SOURCE = -1;
    std::array<int, GAMEPADS_MAX> players;
    SwitchInputManager() { players.fill(NO_SOURCE); }
    void updatePlayers();
    PadState* playerPad(int controller);
};
"""

SCENARIOS = r"""
}
int main() {
    brls::SwitchInputManager input;
    assert(input.getControllersConnectedCount() == 0);
    assert(input.getControllersConnectedMask() == 0);

    input.padStateHandheld.active_handheld = true;
    input.padsState[0].style_set = 1;
    input.padsState[1].style_set = 1;
    assert(input.getControllersConnectedMask() == 0b111);
    assert(input.playerPad(0) == &input.padStateHandheld);
    assert(input.playerPad(1) == &input.padsState[0]);
    assert(input.playerPad(2) == &input.padsState[1]);

    // Player 2 leaves: player 3 stays where it is.
    input.padsState[0].style_set = 0;
    assert(input.getControllersConnectedCount() == 3);
    assert(input.getControllersConnectedMask() == 0b101);
    assert(input.playerPad(1) == nullptr);
    assert(input.playerPad(2) == &input.padsState[1]);

    // A new controller takes the lowest free player.
    input.padsState[3].style_set = 1;
    assert(input.getControllersConnectedMask() == 0b111);
    assert(input.playerPad(1) == &input.padsState[3]);

    // Undocking: the handheld leaves, the pair on No1 takes player 1.
    input.padStateHandheld.active_handheld = false;
    assert(input.getControllersConnectedMask() == 0b110);
    input.padsState[0].style_set = 1;
    assert(input.getControllersConnectedMask() == 0b111);
    assert(input.playerPad(0) == &input.padsState[0]);
    assert(input.playerPad(2) == &input.padsState[1]);

    // More controllers than players: the extra one waits for a free player.
    for (auto& pad : input.padsState)
        pad.style_set = 1;
    input.padStateHandheld.active_handheld = true;
    assert(input.getControllersConnectedMask() == 0b11111);
    input.padsState[2].style_set = 0;
    assert(input.getControllersConnectedMask() == 0b11111);
    assert(input.playerPad(3) == &input.padStateHandheld);
    assert(input.playerPad(-1) == nullptr && input.playerPad(GAMEPADS_MAX) == nullptr);
}
"""


def main():
    cxx = shutil.which("c++") or shutil.which("g++")
    if not cxx:
        raise SystemExit("A host C++ compiler is required for player slot tests")
    source = (ROOT / "library/lib/platforms/switch/switch_input.cpp").read_text()
    players = source[
        source.index("void SwitchInputManager::updatePlayers(") : source.index(
            "void SwitchInputManager::updateControllerState("
        )
    ]
    with tempfile.TemporaryDirectory(prefix="borealis-players-") as name:
        tmp = Path(name)
        test = tmp / "test.cpp"
        test.write_text(ADAPTER + players + SCENARIOS)
        binary = tmp / "test.exe"
        subprocess.run(
            [cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(test), "-o", str(binary)],
            check=True,
        )
        subprocess.run([str(binary)], check=True)
    print("Switch players: stable slots, lowest free player, undock and overflow passed")


if __name__ == "__main__":
    main()
