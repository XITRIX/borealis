#!/usr/bin/env python3
"""Exercise horizontal physical mappings and system-button claims on both Switches."""

import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    cxx = shutil.which("c++") or shutil.which("g++")
    if not cxx:
        raise SystemExit("A host C++ compiler is required for half-Joy-Con tests")
    app = ROOT.parents[1]
    sdk = app / "build/switch2-ci/sdk-20261115b/libounce/include/switch/services/hid.h"
    if not sdk.is_file():
        import os

        sdk = Path(os.environ["DEVKITPRO"]) / "libounce/include/switch/services/hid.h"
    hid = sdk.read_text()
    buttons = re.search(r"typedef enum \{[^}]+\} HidNpadButton;", hid)[0]
    core = (ROOT / "library/include/borealis/core/input.hpp").read_text()
    enum = re.search(r"enum ControllerButton\s*\{[^}]+\};", core)[0]
    scenarios = r"""
#include <cassert>
#include <borealis/platforms/switch/switch_half_joycon.hpp>
using namespace brls;
int main(){
    const auto left=defaultHalfJoyConMapping(HalfJoyConSide::LEFT);
    auto right=defaultHalfJoyConMapping(HalfJoyConSide::RIGHT);
    ControllerState state{};
    assert(halfJoyConSide(1)==HalfJoyConSide::NONE);
    assert(halfJoyConSide(8)==HalfJoyConSide::LEFT&&halfJoyConSide(16)==HalfJoyConSide::RIGHT);
    assert(halfJoyConSystemClaims(left,HalfJoyConSide::LEFT)==1);
    assert(halfJoyConSystemClaims(right,HalfJoyConSide::RIGHT)==2);
    applyHalfJoyConMapping(&state,left,HalfJoyConSide::LEFT,HidNpadButton_Minus,false,false);
    assert(state.buttons[BUTTON_BACK]&&!state.buttons[BUTTON_START]);
    applyHalfJoyConMapping(&state,left,HalfJoyConSide::LEFT,0,true,false);
    assert(state.buttons[BUTTON_START]&&!state.buttons[BUTTON_BACK]);
    applyHalfJoyConMapping(&state,right,HalfJoyConSide::RIGHT,HidNpadButton_Plus,true,false);
    assert(state.buttons[BUTTON_START]&&state.buttons[BUTTON_BACK]);
    applyHalfJoyConMapping(&state,left,HalfJoyConSide::LEFT,HidNpadButton_L|HidNpadButton_ZL,false,false);
    for(int i=0;i<BUTTON_NAV_UP;++i)assert(!state.buttons[i]);
    applyHalfJoyConMapping(&state,right,HalfJoyConSide::RIGHT,HidNpadButton_R|HidNpadButton_ZR,false,false);
    for(int i=0;i<BUTTON_NAV_UP;++i)assert(!state.buttons[i]);
    const int targets[]={BUTTON_X,BUTTON_A,BUTTON_B,BUTTON_Y};
    for(auto side:{HalfJoyConSide::LEFT,HalfJoyConSide::RIGHT}){
        auto profile=defaultHalfJoyConMapping(side);
        auto masks=halfJoyConButtonMasks(side);
        for(int i=0;i<4;++i){
            applyHalfJoyConMapping(&state,profile,side,masks[i],false,false);
            for(int j=0;j<4;++j)assert(state.buttons[targets[j]]==(i==j));
        }
    }
    right[9]=BUTTON_LB;right[10]=BUTTON_LT;
    applyHalfJoyConMapping(&state,right,HalfJoyConSide::RIGHT,HidNpadButton_R|HidNpadButton_ZR,false,false);
    assert(state.buttons[BUTTON_LB]&&state.buttons[BUTTON_LT]);
    right[8]=-1;
    assert(halfJoyConSystemClaims(right,HalfJoyConSide::RIGHT)==0);
    state.buttons[BUTTON_GUIDE]=true;
    applyHalfJoyConMapping(&state,right,HalfJoyConSide::RIGHT,0,false,false);
    assert(state.buttons[BUTTON_GUIDE]); // Unclaimed system input retains menu Guide.
    right[11]=BUTTON_LT;
#ifdef __SWITCH2__
    assert(halfJoyConSystemClaims(right,HalfJoyConSide::RIGHT)==4);
    applyHalfJoyConMapping(&state,right,HalfJoyConSide::RIGHT,0,false,true);
    assert(state.buttons[BUTTON_LT]);
#else
    assert(halfJoyConSystemClaims(right,HalfJoyConSide::RIGHT)==0);
#endif
    assert(validHalfJoyConAction(-1)&&validHalfJoyConAction(BUTTON_LT));
    assert(!validHalfJoyConAction(BUTTON_NAV_UP)&&!validHalfJoyConAction(-2));
}
"""
    with tempfile.TemporaryDirectory(prefix="borealis-half-joycon-") as name:
        tmp = Path(name)
        (tmp / "borealis/core").mkdir(parents=True)
        (tmp / "borealis/core/input.hpp").write_text(
            "#pragma once\nnamespace brls {\n"
            + enum
            + "\nstruct ControllerState {bool buttons[_BUTTON_MAX]={};};\n}\n"
        )
        (tmp / "switch.h").write_text(
            "#pragma once\n#include <cstdint>\nusing u64=uint64_t;using u32=uint32_t;\n"
            "#define BITL(n) (1ULL<<(n))\n"
            + buttons
            + "\nconstexpr u32 HidNpadStyleTag_NpadJoyLeft=8,HidNpadStyleTag_NpadJoyRight=16,HidNpadStyleSet_NpadFullCtrl=7;\n"
        )
        source = tmp / "test.cpp"
        source.write_text(scenarios)
        for platform in ("switch1", "switch2"):
            binary = tmp / (platform + ".exe")
            subprocess.run(
                [
                    cxx,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    *(["-D__SWITCH2__"] if platform == "switch2" else []),
                    "-I",
                    str(tmp),
                    "-I",
                    str(ROOT / "library/include"),
                    str(source),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)
    print(
        "Half Joy-Con defaults, rotation, shoulders, system claims and fallback passed"
    )


if __name__ == "__main__":
    main()
