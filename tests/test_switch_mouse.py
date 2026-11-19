#!/usr/bin/env python3
"""Exercise native mouse polling with USB and Joy-Con SDK responses."""

import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SDK = r"""
#pragma once
#include <cstddef>
#include <cstdint>
using u64=uint64_t; using u32=uint32_t; using s32=int32_t;
using Result=u32;
#define R_FAILED(rc) ((rc)!=0)
#define R_SUCCEEDED(rc) ((rc)==0)
constexpr u32 HidMouseAttribute_IsConnected=2;
constexpr u32 HidMouseButton_Left=1,HidMouseButton_Right=2,HidMouseButton_Middle=4;
constexpr u32 HidMouseButton_Forward=8,HidMouseButton_Back=16;
struct HidMouseState {
    u64 sampling_number=0;
    s32 x=0,y=0,delta_x=0,delta_y=0,wheel_delta_x=0,wheel_delta_y=0;
    u32 buttons=0,attributes=0;
};
inline HidMouseState usb;
inline HidMouseState older[3]; // Queued behind usb, newest first.
inline size_t usbCount=1;
inline void hidInitializeMouse(){}
inline size_t hidGetMouseStates(HidMouseState* out,size_t max){
    assert(usbCount<=max&&usbCount<=4);
    if(usbCount)*out=usb;
    for(size_t i=1;i<usbCount;++i)out[i]=older[i-1];
    return usbCount;
}
#ifdef __SWITCH2__
enum PadPointerSide {PadPointerSide_None,PadPointerSide_Left,PadPointerSide_Right};
enum HidNpadIdType {HidNpadIdType_No1=0,HidNpadIdType_No2=1,HidNpadIdType_No8=7,HidNpadIdType_Handheld=32};
struct PadState{bool active_handheld=false;u32 active_id_mask=1;};
struct HidNmouseHandle {u64 handle=1;};
struct HidNmouseState {u32 flags=2;};
inline u32 pointerFlags=2;
inline HidNpadIdType pointerId=HidNpadIdType_No1;
struct PadPointer {
    bool active=false;
    PadPointerSide side=PadPointerSide_None;
    s32 x=0,y=0,delta_x=0,delta_y=0,wheel_delta_y=0;
    u32 buttons_cur=0,buttons_old=0;
    HidNmouseHandle current;
};
struct HidNmouseButtonConfigJoyLeftJcms {uint8_t selectors[8];};
struct HidNmouseButtonConfigJoyRightJcms {uint8_t selectors[8];};
inline PadPointer nextPointer;
inline Result initializeResult=0,configResult=0;
inline int exits=0,updates=0;
inline void padInitializeDefault(PadState*){}
inline void padUpdate(PadState*){}
inline Result padPointerInitialize(PadPointer*,s32 w,s32 h){
    assert(w==1280&&h==720);return initializeResult;
}
inline void padPointerExit(PadPointer*){++exits;}
inline Result hidSetNmouseButtonConfigJoyLeftJcms(const HidNmouseButtonConfigJoyLeftJcms* c){
    assert(c->selectors[0]==1&&c->selectors[1]==2&&c->selectors[6]==3);return configResult;
}
inline Result hidSetNmouseButtonConfigJoyRightJcms(const HidNmouseButtonConfigJoyRightJcms* c){
    assert(c->selectors[0]==1&&c->selectors[1]==2&&c->selectors[6]==3);return configResult;
}
inline void padPointerUpdate(PadPointer* out,const PadState*){++updates;*out=nextPointer;}
inline Result hidGetNmouseStates(HidNmouseHandle,HidNmouseState* out,size_t,size_t* count){
    out->flags=pointerFlags;*count=1;return 0;
}
inline Result hidGetNmouseNpadIdType(HidNmouseHandle,HidNpadIdType* out){*out=pointerId;return 0;}
#endif
"""

SCENARIOS = r"""
#include <cassert>
#include <cmath>
#include <borealis/platforms/switch/switch_mouse.hpp>
int main(){
    auto near=[](float a,float b){return std::fabs(a-b)<0.0001f;};
    (void)near;
    {
        brls::SwitchMouse mouse;
        assert(mouse.initialize()==0);
        usb={1,100,200,5,-6,1,2,1,2};
        auto state=mouse.update();
        assert(state.x==100&&state.y==200&&state.buttons==1);
        assert(state.delta_x==5&&state.delta_y==-6);
        assert(state.wheel_delta_x==1&&state.wheel_delta_y==2);
        brls::SwitchInputManager input;
        brls::RawMouseState raw;
        input.currentMouseState=state;
        input.updateMouseStates(&raw);
        assert(raw.scroll.x==1&&raw.scroll.y==2); // USB menu scale is unchanged.
        state=mouse.update(); // Same sample: hold buttons, never replay movement/wheel.
        assert(state.buttons==1&&state.delta_x==0&&state.delta_y==0);
        assert(state.wheel_delta_x==0&&state.wheel_delta_y==0);
#ifdef __SWITCH2__
        nextPointer={true,PadPointerSide_Right,500,400,7,8,3,2,0,{1}};
        usb.sampling_number++;
        state=mouse.update();
        assert(near(state.x,100+7/1.5f)&&near(state.y,200+8/1.5f)&&state.buttons==3);
        assert(near(state.delta_x,5+7/1.5f)&&near(state.delta_y,-6+8/1.5f));
        assert(near(state.wheel_delta_y,2+3/120.0f)); // USB clicks plus fine Joy-Con units.
        assert(state.menu_scroll_y==5); // Menu keeps the original raw pixel scale.
        input.currentMouseState=state;
        input.updateMouseStates(&raw);
        assert(raw.scroll.y==5);
        input.setMouseScrollSensitivity(0.1f);
        input.updateMouseStates(&raw);
        assert(near(raw.scroll.y,0.5f));
        input.setMouseScrollSensitivity(2.0f);
        input.updateMouseStates(&raw);
        assert(raw.scroll.y==10);
        assert(near(input.currentMouseState.wheel_delta_y,2+3/120.0f)); // UI gain never changes stream events.
        PadState first,second{false,2},handheld{true,0};
        assert(mouse.mouseSide(&first)==PadPointerSide_Right);
        assert(mouse.mouseSide(&second)==PadPointerSide_None);
        pointerId=HidNpadIdType_Handheld;
        assert(mouse.mouseSide(&handheld)==PadPointerSide_Right);
        assert(mouse.mouseSide(&first)==PadPointerSide_None);
        pointerId=HidNpadIdType_No1;
        nextPointer.delta_x=nextPointer.delta_y=nextPointer.wheel_delta_y=0;
        nextPointer.buttons_old=nextPointer.buttons_cur;
        usb={3,110,210,10,10,0,0,1,2};
        state=mouse.update(); // Idle Joy-Con must not steal the USB cursor.
        assert(state.x==110&&state.y==210&&state.buttons==3);
        nextPointer.wheel_delta_y=12;
        float wheelClicks=0;
        for(int i=0;i<10;++i){state=mouse.update();wheelClicks+=state.wheel_delta_y;}
        assert(near(wheelClicks,1.0f)); // 120 native units become one wheel click.
        nextPointer.wheel_delta_y=-6;
        state=mouse.update();
        assert(near(state.wheel_delta_y,-0.05f)); // Fine reverse motion must survive.
        nextPointer={};
        state=mouse.update(); // Removing Joy-Con releases only its own buttons.
        assert(state.buttons==1&&state.delta_x==0);
        nextPointer={true,PadPointerSide_Left,300,350,4,0,0,1,0,{1}};
        usb={};
        state=mouse.update(); // USB removal preserves the held Joy-Con button.
        assert(state.buttons==1&&near(state.delta_x,4/1.5f)&&near(state.x,110+4/1.5f));
        pointerFlags=0; // Lifting during a drag must preserve the held button.
        nextPointer.delta_x=0;
        state=mouse.update();
        assert(state.buttons==1);
        nextPointer.buttons_cur=0;
        state=mouse.update();
        assert(state.buttons==0&&mouse.mouseSide(&first)==PadPointerSide_None);
        nextPointer.buttons_cur=2; // Ordinary off-surface pad input isn't a mouse click.
        state=mouse.update();
        assert(state.buttons==0);
        pointerFlags=2;
        state=mouse.update();
        assert(state.buttons==2);
        pointerFlags=0;
        nextPointer.current.handle=2; // A different lifted Joy-Con can't inherit a drag.
        state=mouse.update();
        assert(state.buttons==0);
        pointerFlags=2;
        nextPointer={};
#endif
        usbCount=0;
        state=mouse.update(); // No devices: release held buttons, retain cursor.
        assert(state.buttons==0&&state.attributes==0);
        assert(state.delta_x==0&&state.wheel_delta_y==0);
        usbCount=1;
        usb={1,150,250,1,2,0,0,0,2}; // Reconnect may restart sample numbers.
        state=mouse.update();
        assert(state.x==150&&state.y==250&&state.delta_x==1);
        usb={4,150,250,3,-1,0,1,16,2};
        older[0]={3,150,250,2,0,0,1,16,2};
        older[1]={2,150,250,1,0,1,0,0,2};
        older[2]={1,150,250,1,2,0,0,0,2}; // Already read.
        usbCount=4;
        state=mouse.update(); // Every sample since the last update counts.
        assert(state.delta_x==6&&state.delta_y==-1);
        assert(state.wheel_delta_x==1&&state.wheel_delta_y==2&&state.buttons==16);
        input.currentMouseState=state;
        input.updateMouseStates(&raw);
        assert(raw.backButton&&!raw.forwardButton&&!raw.leftButton);
        state=mouse.update();
        assert(state.delta_x==0&&state.wheel_delta_y==0&&state.buttons==16);
        usbCount=1;
#ifdef __SWITCH2__
        mouse.setCursorSpeed(2.0f);
        usb={};
        nextPointer={true,PadPointerSide_Right,999,999,1,0,0,0,0,{1}};
        float relative=0;
        for(int i=0;i<3;++i){state=mouse.update();relative+=state.delta_x;}
        // Three one-pixel samples retain their fraction. Cursor speed applies
        // locally; the streaming consumer applies its own multiplier once.
        assert(near(relative,2.0f)&&near(state.x,154.0f));
        nextPointer.delta_x=10000;
        state=mouse.update();
        assert(state.x==1279&&state.delta_x>6000); // UI clamps, relative motion doesn't.
        nextPointer.delta_x=-10000;
        state=mouse.update();
        assert(state.x==0&&state.delta_x<-6000);
        nextPointer={};
        usb={2,150,250,1,2,0,0,0,2};
#endif
    }
#ifdef __SWITCH2__
    assert(exits==1&&updates>0);
    initializeResult=99;
    {
        brls::SwitchMouse mouse;
        assert(mouse.initialize()==99);
        auto state=mouse.update(); // Failed Joy-Con init must preserve USB input.
        assert(state.x==150&&state.delta_x==1);
    }
    assert(exits==1);
    initializeResult=0;configResult=22;
    {
        brls::SwitchMouse mouse;
        assert(mouse.initialize()==22);
        assert(exits==2);
        assert(mouse.update().x==150);
    }
    assert(exits==2); // Failed configuration releases the service exactly once.
#endif
}
"""


def main():
    cxx = shutil.which("c++") or shutil.which("g++")
    if not cxx:
        raise SystemExit("A host C++ compiler is required for mouse tests")
    source = (ROOT / "library/lib/platforms/switch/switch_input.cpp").read_text()
    update = source[
        source.index("void SwitchInputManager::updateMouseStates(") : source.index(
            "void SwitchInputManager::runloopStart("
        )
    ]
    header = (
        ROOT / "library/include/borealis/platforms/switch/switch_input.hpp"
    ).read_text()
    setter = re.search(r"void setMouseScrollSensitivity\([^}]+\}", header)[0]
    adapter = (
        r"""
#include <borealis/platforms/switch/switch_touch_coordinates.hpp>
namespace brls {
struct Point {
    float x,y;
    Point(float x=0,float y=0):x(x),y(y){}
};
struct RawMouseState {
    Point position,offset,scroll;
    bool leftButton=false,middleButton=false,rightButton=false;
    bool backButton=false,forwardButton=false;
};
struct Application {
    inline static float windowWidth=1280,windowHeight=720,windowScale=1;
};
struct SwitchInputManager {
    SwitchMouseState currentMouseState;
    Point lastCursorPosition;
    float menuScrollSensitivity=1;
    void updateMouseStates(RawMouseState*);
"""
        + setter
        + "\n};\n"
        + update
        + "\n}\n"
    )
    with tempfile.TemporaryDirectory(prefix="borealis-mouse-") as name:
        tmp = Path(name)
        (tmp / "switch.h").write_text(SDK)
        source = tmp / "test.cpp"
        source.write_text(SCENARIOS.replace("int main(){", adapter + "\nint main(){"))
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
    print("Switch 1/2 mouse: motion, clicks, scroll, hotplug and SDK failures passed")


if __name__ == "__main__":
    main()
