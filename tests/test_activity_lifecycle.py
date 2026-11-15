#!/usr/bin/env python3
"""Run the actual activity push/pop methods against a small lifecycle/focus model."""

import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    cxx = shutil.which("c++") or shutil.which("g++")
    if not cxx:
        raise SystemExit("A host C++ compiler is required for activity lifecycle tests")
    source = (ROOT / "library/lib/core/application.cpp").read_text()
    pop = source[
        source.index("bool Application::popActivity(") : source.index(
            "std::vector<Activity*> Application::getActivitiesStack()"
        )
    ]
    push = source[
        source.index("void Application::pushActivity(") : source.index(
            "void Application::clear()"
        )
    ]
    model = r"""
#include <algorithm>
#include <cassert>
#include <functional>
#include <string>
#include <vector>
namespace brls { struct Logger { template<class... T> static void debug(T...){}}; }
using Logger=brls::Logger;
enum class TransitionAnimation {NONE,FADE,SLIDE_LEFT,SLIDE_RIGHT};
struct Activity;
static std::vector<std::string> trace;
struct View {
    Activity* owner;
    bool alive=true;
    std::string name;
    Activity* getParentActivity(){assert(alive);return owner;}
    std::string describe(){assert(alive);return name;}
};
struct Activity {
    View view;
    bool transparent=false,inFade=false,rebuild=false;
    View* replaced=nullptr;
    int appears=0,disappears=0,pauses=0;
    Activity(std::string name,bool transparent=false):view{this,true,name},transparent(transparent){}
    bool isTranslucent(){return transparent||inFade;}
    void willAppear(bool);
    void willDisappear(bool reset){if(!reset)++disappears;}
    void onPause(){++pauses;}
    void onResume(){}
    void setInFadeAnimation(bool value){inFade=value;}
    void hide(std::function<void()> cb,bool,float){cb();}
    void show(std::function<void()> cb,bool,float){cb();}
    float getShowAnimationDuration(TransitionAnimation){return 0.1f;}
    View* createContentView(){return &view;}
    void setContentView(View*){}
    void onContentAvailable(){}
    void resizeToFitWindow(){}
    View* getContentView(){return &view;}
    View* getDefaultFocus(){return &view;}
    int registerExitAction(){return 1;}
};
struct Application {
    inline static std::vector<Activity*> activitiesStack;
    inline static std::vector<View*> focusStack;
    inline static View* currentFocus=nullptr;
    inline static bool globalQuitEnabled=false;
    inline static int gloablQuitIdentifier=0,blocks=0;
    static void blockInputs(){++blocks;}
    static void unblockInputs(){--blocks;assert(blocks>=0);}
    static void giveFocus(View* v){assert(v&&v->alive);currentFocus=v;trace.push_back("focus:"+v->name);}
    static bool popActivity(TransitionAnimation,std::function<void()>,bool);
    static void pushActivity(Activity*,TransitionAnimation);
};
void Activity::willAppear(bool reset){
    if(reset)return;
    ++appears;
    trace.push_back("appear:"+view.name);
    if(rebuild){
        if(replaced)replaced->alive=false;
        Application::giveFocus(&view);
    }
}
"""
    scenarios = r"""
int main(){
    Activity root("host"),stream("stream"),dialog("dialog",true);
    View oldCell{&root,true,"old-app-cell"};
    root.rebuild=true;root.replaced=&oldCell;
    Application::activitiesStack={&root};
    Application::currentFocus=&oldCell;
    assert(!Application::popActivity(TransitionAnimation::NONE,[]{},false));
    Application::pushActivity(&stream,TransitionAnimation::FADE);
    assert(root.disappears==1&&root.pauses==1);
    Application::pushActivity(&dialog,TransitionAnimation::NONE);
    assert(stream.disappears==0);
    assert(Application::popActivity(TransitionAnimation::FADE,[]{},false));
    assert(stream.appears==0); // the translucent overlay did not hide the stream
    trace.clear();
    assert(Application::popActivity(TransitionAnimation::FADE,[]{},false));
    assert(root.appears==1&&!oldCell.alive);
    assert(Application::currentFocus==&root.view);
    assert(trace.size()==3);
    assert(trace[0]=="focus:old-app-cell");
    assert(trace[1]=="appear:host");
    assert(trace[2]=="focus:host");
    assert(Application::focusStack.empty()&&Application::blocks==0);
    dialog.inFade=false;
    const int before=root.disappears;
    Application::pushActivity(&dialog,TransitionAnimation::NONE);
    assert(root.disappears==before);
    assert(Application::popActivity(TransitionAnimation::NONE,[]{},false));
    assert(root.appears==1); // closing a dialog must not reload or loop an error dialog
    assert(Application::blocks==0);
}
"""
    with tempfile.TemporaryDirectory(prefix="borealis-activity-") as directory:
        tmp = Path(directory)
        harness = tmp / "activity.cpp"
        harness.write_text(model + pop + push + scenarios)
        binary = tmp / "activity-test"
        subprocess.run([cxx, "-std=c++17", str(harness), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print(
        "Activity resume appearance, focus replacement and translucent overlay lifecycle passed"
    )


if __name__ == "__main__":
    main()
