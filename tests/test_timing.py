#!/usr/bin/env python3
"""Exercise real Borealis timing code with simulated SDK clocks and frame cadences."""

import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def between(source, start, end):
    return source[source.index(start) : source.index(end, source.index(start))]


def main():
    cxx = shutil.which("c++") or shutil.which("g++")
    if not cxx:
        raise SystemExit("A host C++ compiler is required for timing tests")
    cpu = (
        ROOT / "library/lib/extern/libretro-common/features/features_cpu.c"
    ).read_text()
    clock = between(
        cpu, "retro_time_t cpu_features_get_time_usec(void)", "#if defined(__x86_64__)"
    )
    app = (ROOT / "library/lib/core/application.cpp").read_text()
    frame = between(
        app,
        "bool Application::internalMainLoop()",
        "    Application::setActiveEvent(false);",
    )
    frame = (
        frame.replace(
            "bool Application::internalMainLoop()", "void Application::beginFrame()"
        )
        + "}\n"
    )
    frame += between(
        app, "float Application::getFrameTime()", "void Application::setLimitedFPS("
    )
    repeat = between(
        app, "    // Trigger controller events", "    // Trigger keyboard events"
    )
    constants = "\n".join(
        re.findall(
            r"^#define (?:BUTTOM_REPEAT_TRIGGER|BUTTON_REPEAT_DELAY).*",
            app,
            re.MULTILINE,
        )
    )
    slider = (ROOT / "library/lib/views/slider.cpp").read_text()
    movement = between(
        slider, "void Slider::buttonsProcessing()", "void Slider::setProgress("
    )
    for cls, file, getter, limit in (
        ("ScrollingFrame", "scrolling_frame.cpp", "getContentOffsetY", "bottomLimit"),
        ("HScrollingFrame", "h_scrolling_frame.cpp", "getContentOffsetX", "rightLimit"),
    ):
        source = (ROOT / "library/lib/views" / file).read_text()
        switch = between(
            source,
            "    switch (focusDirection)",
            f"    setContentOffset{'Y' if limit == 'bottomLimit' else 'X'}(newOffset, false);",
        )
        movement += f"float {cls}::move(FocusDirection focusDirection) {{\nfloat {limit}=10000, newOffset=0; bool isBorder=false;\n{switch}\n(void)isBorder;return newOffset;}}\n"

    model = r"""
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include <borealis/core/time.hpp>
namespace brls {
enum ControllerButton {BUTTON_NAV_LEFT,BUTTON_NAV_RIGHT,_BUTTON_MAX};
enum class FocusDirection {UP,DOWN,LEFT,RIGHT};
struct ControllerState {bool buttons[_BUTTON_MAX]={};Time repeatingButtonStop[_BUTTON_MAX]={};};
constexpr int SOUND_FOCUS_ERROR=0;
struct Pointer {bool isFocused(){return true;} void shakeHighlight(FocusDirection){}};
struct Audio {void play(int){}};
struct Application {
    inline static Time frameStartTime=0,frameDeltaTime=0;
    inline static ControllerState controllerState;
    inline static std::vector<Time> presses;
    static bool hasActiveEvent(){return true;}
    static void updateFPS(){}
    static void beginFrame();
    static float getFrameTime();
    static const ControllerState& getControllerState(){return controllerState;}
    static void onControllerButtonPressed(ControllerButton,bool){presses.push_back(getCPUTimeUsec());}
    static Audio* getAudioPlayer(){static Audio audio;return &audio;}
};
struct Slider {
    Pointer value;Pointer* pointer=&value;float progress=0.25f,step=0.5f;
    void setProgress(float value){progress=std::clamp(value,0.0f,1.0f);}
    void buttonsProcessing();
};
struct ScrollingFrame {float getContentOffsetY(){return 0;}float move(FocusDirection);};
struct HScrollingFrame {float getContentOffsetX(){return 0;}float move(FocusDirection);};
}
"""
    gesture = r"""
#pragma once
namespace brls {
struct Point {float x=0,y=0;Point()=default;Point(float x,float y):x(x),y(y){}
Point operator-(Point p)const{return {x-p.x,y-p.y};}};
enum class GestureState {INTERRUPTED,UNSURE,START,STAY,END,FAILED};
enum class TouchPhase {NONE,START,STAY,END};
struct TouchState {TouchPhase phase=TouchPhase::NONE;Point position;int fingerId=0;};
struct MouseState {Point position;TouchPhase leftButton=TouchPhase::NONE;};
struct View {};using Sound=int;
struct GestureRecognizer {
virtual ~GestureRecognizer()=default;
virtual GestureState recognitionLoop(TouchState,MouseState,View*,Sound*)=0;
GestureState state=GestureState::FAILED;bool enabled=true;
};
}
"""
    scenarios = r"""
#include <cassert>
#include <iostream>
#include <borealis.hpp>
#include <borealis/core/animation.hpp>
#include <borealis/core/timer.hpp>
static uint64_t ticks=0;
static int frequency=0;
// SDK mock: Switch 1 has 19.2 MHz ticks; Switch 2 has 31.25 MHz ticks.
static uint64_t armGetSystemTick(){return ticks;}
static uint64_t armTicksToNs(uint64_t t){return frequency==31250000?t*32:t*625/12;}
#undef _WIN32
#define HAVE_LIBNX
extern "C" {
CLOCK
}
namespace brls {
FRAME
CONSTANTS
void processButtons(){
    auto& controllerState=Application::controllerState;
    static ControllerState oldControllerState;
REPEAT
}
MOVEMENT
}
using namespace brls;
static void at(Time us){ticks=uint64_t(us/1000000)*frequency+uint64_t(us%1000000)*frequency/1000000;}
static void near(float a,float b,float tolerance=0.002f){if(std::fabs(a-b)>=tolerance)std::cerr<<"Expected "<<b<<", got "<<a<<" at "<<frequency<<" Hz\n";assert(std::fabs(a-b)<tolerance);}
static void run(const std::vector<Time>& frames,Time base){
    at(base);Application::frameStartTime=0;Application::beginFrame();
    assert(Application::getFrameTime()==0);
    Ticking::updateTickings();
    Animatable animation;animation.reset(0);animation.addStep(1,1000);animation.start();
    Timer timer;bool fired=false;timer.setEndCallback([&](bool done){fired=done;});timer.start(500);
    Slider slider;
    ScrollingFrame vertical;HScrollingFrame horizontal;
    float x=0,y=0;
    Application::controllerState={};processButtons();Application::presses.clear();
    Application::controllerState.buttons[BUTTON_NAV_RIGHT]=true;processButtons();
    Time elapsed=0;
    for(Time dt:frames){
        elapsed+=dt;at(base+elapsed);Application::beginFrame();Ticking::updateTickings();
        processButtons();slider.buttonsProcessing();
        x+=horizontal.move(FocusDirection::RIGHT);y+=vertical.move(FocusDirection::DOWN);
        near(animation.getValue(),std::min(elapsed/1000000.0f,1.0f));
        assert(fired==(elapsed>=500000));
    }
    near(slider.progress,0.75f);near(x,1000);near(y,1000);
    const Time trigger=BUTTOM_REPEAT_TRIGGER,delay=BUTTON_REPEAT_DELAY;
    const Time maxFrame=*std::max_element(frames.begin(),frames.end());
    assert(Application::presses.size()>=7);
    Time first=Application::presses[1]-base;
    assert(first>trigger&&first<=trigger+maxFrame+1);
    for(size_t i=2;i<Application::presses.size();++i){
        Time diff=Application::presses[i]-Application::presses[i-1];
        assert(diff>delay&&diff<=delay+maxFrame+1);
    }
    // Releasing cancels repeat and a new press starts a fresh hold interval.
    Application::controllerState={};processButtons();
    Application::controllerState.buttons[BUTTON_NAV_RIGHT]=true;processButtons();
    assert(Application::controllerState.repeatingButtonStop[BUTTON_NAV_RIGHT]==base+elapsed+trigger);
}
static void swipe(const std::vector<Time>& frames,Time base){
    PanAcceleration end{};
    PanGestureRecognizer pan([&](PanGestureStatus s,Sound*){if(s.state==GestureState::END)end=s.acceleration;},PanAxis::ANY);
    TouchState touch;touch.phase=TouchPhase::START;Sound sound=0;
    at(base);pan.recognitionLoop(touch,{},nullptr,&sound);
    Time elapsed=0;
    for(Time dt:frames){
        elapsed+=dt;at(base+elapsed);touch.phase=TouchPhase::STAY;
        touch.position={elapsed*0.0006f,0};pan.recognitionLoop(touch,{},nullptr,&sound);
    }
    touch.phase=TouchPhase::END;pan.recognitionLoop(touch,{},nullptr,&sound);
    near(end.time.x,0.2f);near(end.distance.x,-60,0.01f);
    // Duplicate timestamps must yield finite, zero inertia.
    touch.phase=TouchPhase::START;pan.recognitionLoop(touch,{},nullptr,&sound);
    for(int i=0;i<6;++i){touch.phase=TouchPhase::STAY;touch.position.x+=10;pan.recognitionLoop(touch,{},nullptr,&sound);}
    touch.phase=TouchPhase::END;pan.recognitionLoop(touch,{},nullptr,&sound);
    assert(end.time.x==0&&end.distance.x==0);
}
int main(){
    Time base=1000000000000LL;
    for(int hz:{19200000,31250000}){
        frequency=hz;at(1000000);assert(getCPUTimeUsec()==1000000);
        at(base);assert(getCPUTimeUsec()==base);
        for(int fps:{30,60,120,144}){
            std::vector<Time> frames;
            for(int i=1;i<=fps;++i)frames.push_back(i*1000000LL/fps-(i-1)*1000000LL/fps);
            run(frames,base);base+=2000000;swipe(frames,base);base+=2000000;
        }
        std::vector<Time> variable;
        for(int i=0;i<20;++i)for(Time dt:{5000,8000,17000,20000})variable.push_back(dt);
        run(variable,base);base+=2000000;swipe(variable,base);base+=2000000;
    }
    std::cout<<"Timing host mocks: Switch 1/2 clocks; animations, timers, holds, sliders, scroll and inertia at 30/60/120/144 FPS and variable cadence passed\n";
}
"""
    for token, value in (
        ("CLOCK", clock),
        ("FRAME", frame),
        ("CONSTANTS", constants),
        ("REPEAT", repeat),
        ("MOVEMENT", movement),
    ):
        scenarios = scenarios.replace(f"\n{token}\n", f"\n{value}\n")
    with tempfile.TemporaryDirectory(prefix="borealis-timing-") as directory:
        tmp = Path(directory)
        (tmp / "borealis/core").mkdir(parents=True)
        (tmp / "borealis/core/application.hpp").write_text(model)
        (tmp / "borealis/core/gesture.hpp").write_text(gesture)
        (tmp / "borealis.hpp").write_text(
            "#include <borealis/core/application.hpp>\n"
            "#include <borealis/core/touch/pan_gesture.hpp>\n"
        )
        (tmp / "test.cpp").write_text(scenarios)
        binary = tmp / "timing.exe"
        subprocess.run(
            [
                cxx,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Wno-unused-parameter",
                "-Wno-missing-field-initializers",
                "-I",
                str(tmp),
                "-I",
                str(ROOT / "library/include"),
                "-I",
                str(ROOT / "library/include/borealis/extern"),
                "-I",
                str(ROOT / "library/lib/extern/tweeny/include"),
                str(tmp / "test.cpp"),
                *[
                    str(ROOT / "library/lib/core" / file)
                    for file in (
                        "time.cpp",
                        "timer.cpp",
                        "animation.cpp",
                        "touch/pan_gesture.cpp",
                    )
                ],
                "-o",
                str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
