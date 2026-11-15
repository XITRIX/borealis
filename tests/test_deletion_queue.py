#!/usr/bin/env python3
"""Exercise actual frame/exit deletion code with destructors that enqueue views."""

import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    cxx = shutil.which("c++") or shutil.which("g++")
    if not cxx:
        raise SystemExit("A host C++ compiler is required for deletion queue tests")
    source = (ROOT / "library/lib/core/application.cpp").read_text()
    frame = source[
        source.index("    // Free views deletion pool.") : source.index(
            "    if (Application::limitedFrameTime > 0)"
        )
    ]
    shutdown = source[
        source.index(
            "    // Free views deletion pool", source.index("void Application::exit()")
        ) : source.index(
            "    ThreadPool::shutdownGlobal();",
            source.index("void Application::exit()"),
        )
    ]
    enqueue = source[
        source.index("void Application::addToFreeQueue(") : source.index(
            "void Application::tryDeinitFirstResponder("
        )
    ]
    shutdown_method = source[
        source.index("void Application::exit()") : source.index(
            "void Application::setGlobalQuit("
        )
    ]
    model = r"""
#include <algorithm>
#include <cassert>
#include <deque>
#include <string>
#include <vector>
namespace brls {struct Logger {
    template<class... T>static void verbose(T...){}
    template<class... T>static void info(T...){}
};}
using Logger=brls::Logger;
static bool gpuIdle=false,requireGpuIdle=false,workersStopped=false,threadingStopped=false;
struct Video {void waitIdle(){gpuIdle=true;}};
struct Platform {Video video;Video* getVideoContext(){return &video;}};
struct Event {void fire(){}};
struct ThreadPool {static void shutdownGlobal(){workersStopped=true;}};
struct Threading {static void stop(){threadingStopped=true;}};
struct View;
struct Application {
    inline static std::deque<View*> deletionPool;
    inline static Platform* platform=nullptr;
    inline static int* notificationManager=nullptr;
    inline static Event exitEvent,exitDoneEvent;
    static void addToFreeQueue(View*);
    static void frame();
    static void shutdown();
    static void clear();
    static void exit();
};
static int deleted=0;
struct View {
    bool locked=false;
    View* child=nullptr;
    bool isPtrLocked(){return locked;}
    std::string describe(){return "test-view";}
    ~View(){assert(!requireGpuIdle||gpuIdle);++deleted;if(child)Application::addToFreeQueue(child);}
};
void Application::clear(){assert(gpuIdle);addToFreeQueue(new View{false,new View{}});}
"""
    scenarios = r"""
int main(){
    // A destructor may enqueue a locked child. Keep it for a later frame.
    auto* child=new View{true,nullptr};
    Application::addToFreeQueue(new View{false,child});
    Application::frame();
    assert(deleted==1&&Application::deletionPool.size()==1);
    Application::frame();
    assert(deleted==1&&Application::deletionPool.size()==1);
    child->locked=false;
    Application::frame();
    assert(deleted==2&&Application::deletionPool.empty());
    // Queue order is insertion order, not pointer order; duplicate scheduling
    // must not cause a double delete when parents share a deferred child.
    std::vector<View*> views;
    for(int i=0;i<100;++i)views.push_back(new View{});
    for(int i=99;i>=0;--i)Application::addToFreeQueue(views[i]);
    for(int i=0;i<100;++i)Application::addToFreeQueue(views[i]);
    assert(Application::deletionPool.size()==100);
    Application::shutdown();
    assert(deleted==102&&Application::deletionPool.empty());
    // Shutdown must also drain descendants added during destruction, even
    // across deque storage growth and without another main-loop iteration.
    for(int i=0;i<100;++i)
        Application::addToFreeQueue(new View{false,new View{true,new View{}}});
    Application::shutdown();
    assert(deleted==402&&Application::deletionPool.empty());
    requireGpuIdle=true;
    Application::platform=new Platform{};
    Application::notificationManager=new int{};
    Application::exit();
    assert(deleted==404&&Application::deletionPool.empty());
    assert(gpuIdle&&workersStopped&&threadingStopped&&!Application::platform);
}
"""
    with tempfile.TemporaryDirectory(prefix="borealis-deletion-") as folder:
        tmp = Path(folder)
        harness = tmp / "deletion.cpp"
        harness.write_text(
            model
            + enqueue
            + "void Application::frame(){\n"
            + frame
            + "}\nvoid Application::shutdown(){\n"
            + shutdown
            + "}\n"
            + shutdown_method
            + scenarios
        )
        binary = tmp / "deletion-test.exe"
        subprocess.run(
            [cxx, "-std=c++17", "-D_GLIBCXX_DEBUG", str(harness), "-o", str(binary)],
            check=True,
        )
        result = subprocess.run(
            [str(binary)], check=False, capture_output=True, text=True
        )
        if result.returncode:
            raise SystemExit(
                result.stderr or f"Deletion regression failed: {result.returncode}"
            )
    print(
        "Frame and shutdown deletion: nested enqueue, locks and duplicate scheduling passed"
    )


if __name__ == "__main__":
    main()
