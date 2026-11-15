# Switch 2 native Vulkan backend

Build with the Ounce SDK (`libounce`, OunceVK and devkitA64):

```sh
git submodule update --init --recursive
export DEVKITPRO=/path/to/devkitPro
cmake -S . -B build-switch2 -G Ninja -DPLATFORM_SWITCH2=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-switch2 --target borealis_demo.nro
```

`PLATFORM_SWITCH2` selects SDL3 for lifecycle/window/input and native Vulkan for
NanoVG drawing. SDL3 is built from the pinned XITRIX/SDL fork. No patches are
applied by consuming applications. The original `PLATFORM_SWITCH` target and its
deko3d/OpenGL options continue to use libnx.

The backend follows libounce's native output size, including TV-resolution
changes, and requests the platform's 120 Hz handheld / 60 Hz docked policy.
Physical dock/TV mode switching still needs device testing.

Borealis uses the SDK's `armTicksToNs(armGetSystemTick())` conversion for its
microsecond clock. The counter is 19.2 MHz on Switch 1 and 31.25 MHz on Switch 2;
the old fixed Switch 1 conversion ran animations and button repeat about 1.63
times too fast on Switch 2. Animations and the 250 ms initial / 100 ms subsequent
button repeats use elapsed time. Continuous slider/scroll movement uses measured
main-loop time, and swipe inertia uses timestamps from the actual touch samples,
independent of the averaged FPS display. `tests/test_timing.py` exercises the real
timing and movement code with mocked SDK clocks at 30/60/120/144 FPS and variable
cadence. These host checks do not establish device timing or perceived UI feel.

The native input backend accepts either Joy-Con 2 of the primary controller as
a mouse, alongside USB mice. L/R clicks, ZL/ZR right-clicks, pressing the stick
middle-clicks, and the stick scrolls. Movement is read once per input loop through
libounce's `PadPointer`; applications receive the same mouse state and events
as USB input. Pointer coordinates follow the UI scale in handheld and docked modes.
An unavailable Joy-Con pointer service leaves USB input usable.
Joy-Con movement defaults to two thirds of the native delta. Fractional movement
is preserved; `setJoyConMouseCursorSpeed` applies an application's speed setting
to the UI cursor without applying it again to relative streaming events.
Menu scrolling retains native pixel deltas independently of normalized wheel
events. `setMouseScrollSensitivity` changes menu scrolling; applications apply
their stream sensitivity to wheel events independently, once per consumer.

The Switch 2 C (Chat) button uses `hid:sys` command 171 to acquire its event,
with the same PID, applet-resource ID and copied-handle ABI as Home/Capture.
The command identification is recorded in the
[IHidSystemServer implementation](https://github.com/vexcited/fefw/blob/c57bc31f85bb67a322b816ceeb53d4027fd50b3a/src/nn/hid/detail/hid_server_proxy.cpp).
`switch_chat_button.hpp` supplies the wrapper missing from the pinned libounce
SDK. `SwitchInputManager` exposes Chat press state and override modes on Switch 2.
It follows the existing Home/Capture event-clearing interception mechanism;
`NONE` leaves normal system handling enabled. Acquiring Chat is optional, and
failure keeps the Home/Capture worker running. Shared modes and button states
are atomic, and disabling an override clears its held state.

`VideoContext::waitIdle` lets asynchronous backends finish graphics work before
application shutdown destroys views and their textures. Switch 2 implements it
with the device idle wait under the queue lock. Shutdown logs identify each
teardown stage and completion. The deferred deletion queue uses pop-front
iteration so destructors can safely enqueue descendants; duplicate scheduling
uses linear lookup because queue insertion order is not sorted. Checked-iterator
regressions in `test_deletion_queue.py` cover frame/exit draining, locks, duplicate
requests and the graphics-before-view destruction order.

On both Switch targets, `setHalfJoyConMapping` configures separate horizontal
left/right profiles. `setHalfJoyConMappingEnabled` lets an application activate
them only while forwarding game input; ordinary UI controls remain unchanged.
`half_joycon_mapping.hpp` defines SDK-independent action arrays and defaults;
`switch_half_joycon.hpp` projects physical buttons into a controller state.
Unassigned Home/Capture/C bindings fall through to the existing override mode,
then system handling. Assigned bindings consume the event before overlay/Guide
handling and are delivered only to their controller. The event worker latches
the last-active Npad through release, using a unique physically capable source
only when the reported controller is unavailable or incapable. It does not guess
among several possible sources. Atomic claim snapshots avoid reading PadState
or mutable mappings on the event thread. `test_half_joycon.py` and
`test_system_buttons.py` cover both target configurations.

Applications drawing video or other custom Vulkan content can use
`brls::Switch2VulkanContext::current()` from
`borealis/platforms/switch/switch2_vulkan.hpp`. Its command buffer is inside the
current render pass. Set all dynamic pipeline state required by custom drawing.
Call `retain(std::shared_ptr<void>)` to keep an external allocation owner alive
until the current submission's fence completes. No FFmpeg dependency is required
by Borealis. Imported host buffers must satisfy the device's external-memory
alignment and lifetime requirements; the caller remains responsible for waiting
for external producers before submitting their consumers.

The NanoVG Vulkan backend lives in `library/lib/extern/nanovg-vulkan`, with its
upstream revision, license and local changes recorded alongside the source.
