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
