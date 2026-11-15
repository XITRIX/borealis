#!/usr/bin/env python3
"""Exercise the actual system-button worker and native C-button IPC wrapper."""

import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SDK = r"""
#pragma once
#include <cassert>
#include <cstdint>
#include <vector>
using u64=uint64_t;using s32=int32_t;using u32=uint32_t;using Handle=u32;using Result=u32;
constexpr Handle INVALID_HANDLE=0;
constexpr int CUR_THREAD_HANDLE=1;
constexpr int GAMEPADS_MAX=5;
struct PadState {};
constexpr int SfOutHandleAttr_HipcCopy=2;
#define R_FAILED(rc) ((rc)!=0)
#define R_SUCCEEDED(rc) ((rc)==0)
#define KERNELRESULT(value) 1
struct Event {Handle handle=0;bool autoclear=false;};
struct Service{};
struct SfDispatchParams {bool in_send_pid;u32 out_handle_attrs[1];Handle* out_handles;};
inline Result chatResult=0,captureResult=0,homeResult=0;
inline Result ownerResult=0;
inline u32 activePad=0;
inline Result hidsysGetLastActiveNpad(u32* out){*out=activePad;return ownerResult;}
inline int loads=0,closes=0;
inline std::vector<Handle> cleared;
inline u64 appletGetAppletResourceUserId(){return 0x12345678;}
inline Service* hidsysGetServiceSession(){static Service service;return &service;}
inline Result dispatch(Service* service,u32 command,u64 appletId,SfDispatchParams params){
    assert(service==hidsysGetServiceSession()&&command==171);
    assert(appletId==0x12345678&&params.in_send_pid);
    assert(params.out_handle_attrs[0]==SfOutHandleAttr_HipcCopy);
    if(chatResult)return chatResult;
    *params.out_handles=171;return 0;
}
#define serviceDispatchIn(s,c,in,...) dispatch(s,c,in,SfDispatchParams{__VA_ARGS__})
inline void eventLoadRemote(Event* event,Handle handle,bool autoclear){
    ++loads;*event={handle,autoclear};
}
inline void eventClear(Event* event){assert(event->handle);cleared.push_back(event->handle);}
inline void eventClose(Event* event){assert(event->handle);++closes;event->handle=0;}
inline Result hidsysAcquireCaptureButtonEventHandle(Event* event,bool autoclear){
    if(captureResult)return captureResult;
    *event={141,autoclear};return 0;
}
inline Result hidsysAcquireHomeButtonEventHandle(Event* event,bool autoclear){
    if(homeResult)return homeResult;
    *event={101,autoclear};return 0;
}
inline Result svcSetThreadPriority(Handle,int priority){assert(priority==0x20);return 0;}
struct Waiter {Handle handle;};
inline Waiter waiterForEvent(Event* event){return {event->handle};}
"""

MODEL = r"""
#include <atomic>
#include <array>
#include <chrono>
#include <functional>
#include <stop_token>
#include <tuple>
#include <switch.h>
#include <borealis/platforms/switch/switch_chat_button.hpp>
using NXEvent=Event;
using namespace std::chrono_literals;
namespace brls {
struct Logger {
    template<class... T>static void info(T...){}
    template<class... T>static void warning(T...){}
    template<class... T>static void error(T...){}
};
}
using Logger=brls::Logger;
#ifdef __SWITCH2__
using brls::acquireSwitchChatButtonEvent;
#endif
struct Defer {std::function<void()> action;~Defer(){action();}};
#define JOIN_IMPL(a,b) a##b
#define JOIN(a,b) JOIN_IMPL(a,b)
#define DEFER(f) Defer JOIN(defer_,__COUNTER__){f}
enum class ButtonOverrideMode {NONE,CUSTOM_EVENT,GUIDE_BUTTON};
constexpr int BUTTON_GUIDE=0;
struct ControllerState {bool buttons[1]={};};
static std::vector<int> sequence;
static size_t cursor;
static int expectedCount;
static Result waitResult;
static std::stop_source* stop;
static std::function<void()> observe;
Result waitObjects(s32* index,const Waiter* events,s32 count,u64 timeout){
    assert(count==expectedCount&&timeout==50000000);
    for(int i=0;i<count;++i)assert(events[i].handle);
    if(observe)observe();
    if(cursor==sequence.size()){stop->request_stop();return 1;}
    *index=sequence[cursor++];return waitResult;
}
struct SwitchInputManager {
    std::atomic<bool> halfMappingEnabled=false;
    std::array<std::atomic<u32>,5> halfSystemClaims={};
    std::array<std::atomic<u64>,3> halfSourceCandidates={};
    std::atomic<u32> halfAnyClaims=0;
    std::array<std::atomic<bool>,3> halfSystemConsumed={};
    std::array<std::atomic<int>,3> systemButtonOwners={-1,-1,-1};
    std::atomic<ButtonOverrideMode> m_screenshotButtonMode=ButtonOverrideMode::NONE;
    std::atomic<ButtonOverrideMode> m_homeButtonMode=ButtonOverrideMode::NONE;
    std::atomic<bool> m_isScreenshotPressed=false,m_isHomePressed=false;
#ifdef __SWITCH2__
    std::atomic<ButtonOverrideMode> m_chatButtonMode=ButtonOverrideMode::NONE;
    std::atomic<bool> m_isChatPressed=false;
#endif
    void system_button_thread_fn(std::stop_token);
    void processSystemButtonEvent(::Event*,int);
    void applyHalfMapping(ControllerState*,PadState*,u64){}
"""

SCENARIOS = r"""
void run(SwitchInputManager& input,std::vector<int> events,int count){
    std::stop_source source;stop=&source;
    cursor=0;sequence=events;expectedCount=count;
    cleared.clear();loads=closes=0;
    input.system_button_thread_fn(source.get_token());
}
int main(){
    SwitchInputManager input;
    input.setScreenshotButtonOverrideMode(ButtonOverrideMode::CUSTOM_EVENT);
    input.setHomeButtonOverrideMode(ButtonOverrideMode::CUSTOM_EVENT);
    std::vector<std::tuple<bool,bool,bool>> trace;
    observe=[&]{if(cursor)trace.emplace_back(input.isHomeButtonPressed(),input.isScreenshotButtonPressed(),input.guide());};
#ifdef __SWITCH2__
    run(input,{1,1,0,0},3);
    assert(closes==3&&trace.size()==4);
#else
    run(input,{1,1,0,0},2);
    assert(closes==2&&trace.size()==4);
#endif
    assert(std::get<0>(trace[0])&&!std::get<0>(trace[1]));
    assert(std::get<1>(trace[2])&&!std::get<1>(trace[3]));
#ifdef __SWITCH2__
    input.setChatButtonOverrideMode(ButtonOverrideMode::GUIDE_BUTTON);
    std::vector<std::pair<bool,bool>> chatTrace;
    observe=[&]{if(cursor)chatTrace.emplace_back(input.isChatButtonPressed(),input.guide());};
    run(input,{2,2},3);
    assert(closes==3&&loads==1&&chatTrace.size()==2);
    assert(chatTrace[0]==std::make_pair(true,true));
    assert(chatTrace[1]==std::make_pair(false,false));
    input.m_isChatPressed=true;
    input.setChatButtonOverrideMode(ButtonOverrideMode::NONE);
    assert(!input.isChatButtonPressed()&&!input.guide());
    observe={};
    run(input,{2,2},3);
    int chatClears=0;for(auto h:cleared)if(h==171)++chatClears;
    assert(chatClears==1); // Startup only: disabled interception leaves AM's events alone.
    chatResult=9;
    input.setHomeButtonOverrideMode(ButtonOverrideMode::GUIDE_BUTTON);
    trace.clear();
    observe=[&]{if(cursor)trace.emplace_back(input.isHomeButtonPressed(),false,input.guide());};
    run(input,{1,1},2);
    assert(closes==2&&loads==0&&std::get<2>(trace[0])&&!std::get<2>(trace[1]));
    chatResult=0;
#endif
    observe={};
    homeResult=9;
    run(input,{},2);
    assert(closes==1); // Only the successfully acquired Capture handle closes.
    homeResult=0;captureResult=9;
    run(input,{},2);
    assert(closes==0); // Never close an uninitialized event.
    captureResult=0;waitResult=9;
#ifdef __SWITCH2__
    run(input,{0},3);
    assert(closes==3);
#else
    run(input,{0},2);
    assert(closes==2);
#endif
    assert(!input.isHomeButtonPressed()&&!input.isScreenshotButtonPressed());
    waitResult=0;chatResult=0;
    input.halfMappingEnabled=true;
    input.halfSystemClaims[0]=1;input.halfAnyClaims=1;
    input.halfSourceCandidates[0]=1;
    input.setScreenshotButtonOverrideMode(ButtonOverrideMode::GUIDE_BUTTON);
    bool halfDown=false;
    observe=[&]{if(cursor==1){halfDown=input.m_isScreenshotPressed&&input.halfSystemConsumed[0];assert(!input.guide());assert(!input.isScreenshotButtonPressed());}};
#ifdef __SWITCH2__
    run(input,{0,0},3);
#else
    run(input,{0,0},2);
#endif
    assert(halfDown);
    activePad=1;
    input.halfSourceCandidates[0]=3;
    bool menuDown=false;
    observe=[&]{if(cursor==1)menuDown=input.isScreenshotButtonPressed()&&input.guide();};
#ifdef __SWITCH2__
    run(input,{0,0},3);
#else
    run(input,{0,0},2);
#endif
    assert(menuDown);
    // A stale last-active right half cannot own Capture. A unique physical
    // source is safe to infer, while multiple possible sources remain ambiguous.
    input.halfSourceCandidates[0]=1;
    halfDown=false;
    observe=[&]{if(cursor==1)halfDown=input.halfSystemConsumed[0]&&input.systemButtonOwners[0]==0;};
#ifdef __SWITCH2__
    run(input,{0,0},3);
#else
    run(input,{0,0},2);
#endif
    assert(halfDown);
#ifdef __SWITCH2__
    input.halfSystemClaims[0]=6;input.halfAnyClaims=6;
    input.halfSourceCandidates[1]=input.halfSourceCandidates[2]=1;
    input.setChatButtonOverrideMode(ButtonOverrideMode::CUSTOM_EVENT);
    input.setHomeButtonOverrideMode(ButtonOverrideMode::GUIDE_BUTTON);
    bool homeDown=false,chatDown=false;
    observe=[&]{
        if(cursor==1)homeDown=input.halfSystemConsumed[1]&&!input.isHomeButtonPressed()&&!input.guide();
        if(cursor==3)chatDown=input.halfSystemConsumed[2]&&!input.isChatButtonPressed()&&!input.guide();
    };
    run(input,{1,1,2,2},3);
    assert(homeDown&&chatDown);
    input.halfSystemClaims[0]=1;input.halfAnyClaims=1;
#endif
    observe={};ownerResult=9;
    input.halfSourceCandidates[0]=3;
    input.halfSystemClaims[1]=1;
    input.setScreenshotButtonOverrideMode(ButtonOverrideMode::NONE);
#ifdef __SWITCH2__
    run(input,{0,0},3);
#else
    run(input,{0,0},2);
#endif
    int captureClears=0;for(auto h:cleared)if(h==141)++captureClears;
    assert(captureClears==1);
}
"""


def main():
    cxx = shutil.which("c++") or shutil.which("g++")
    if not cxx:
        raise SystemExit("A host C++ compiler is required for system-button tests")
    source = (ROOT / "library/lib/platforms/switch/switch_input.cpp").read_text()
    header = (
        ROOT / "library/include/borealis/platforms/switch/switch_input.hpp"
    ).read_text()
    worker = source[
        source.index(
            "void SwitchInputManager::processSystemButtonEvent("
        ) : source.rindex("} // namespace brls")
    ]
    methods = header[
        header.index("bool isScreenshotButtonPressed()") : header.index(
            "  private:", header.index("bool isScreenshotButtonPressed()")
        )
    ]
    guide = source[
        source.index("    state->buttons[BUTTON_GUIDE] = false;") : source.index(
            "    HidAnalogStickState analog_stick_l ="
        )
    ]
    with tempfile.TemporaryDirectory(prefix="borealis-system-buttons-") as name:
        tmp = Path(name)
        (tmp / "switch.h").write_text(SDK)
        harness = tmp / "test.cpp"
        harness.write_text(
            MODEL
            + methods
            + "bool guide(){ControllerState value;auto* state=&value;PadState* pad=nullptr;u64 keysDown=0;\n"
            + guide
            + "return value.buttons[BUTTON_GUIDE];}\n};\n"
            + worker
            + SCENARIOS
        )
        for platform in ("switch1", "switch2"):
            binary = tmp / (platform + ".exe")
            subprocess.run(
                [
                    cxx,
                    "-std=c++20",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    *(["-D__SWITCH2__"] if platform == "switch2" else []),
                    "-I",
                    str(tmp),
                    "-I",
                    str(ROOT / "library/include"),
                    str(harness),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)
    print(
        "System buttons: C IPC, guide mapping, passthrough, fallback and cleanup passed"
    )


if __name__ == "__main__":
    main()
