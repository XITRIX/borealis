# Borealis Switch 2 integration

Upstream provenance and file hashes are recorded in upstream.json. The upstream
license is retained in LICENSE.txt. The C backend is compiled against the
Borealis's existing NanoVG core, with its UI API unchanged.

Local changes to src/nanovg_vk.c:
- Request HOST_VISIBLE | HOST_COHERENT memory rather than also requiring
  DEVICE_LOCAL: the installed OunceVK exposes coherent UMA memory without that bit.
- Log and terminate on Vulkan errors even in release builds; NDEBUG must not
  remove error handling.

Local change to src/nanovg_vk.h:
- Include Vulkan directly so the public header declares its own types and works
  independently of the consuming application's include order.

UI SPIR-V is the pinned upstream output, validated by spirv-val during every
Switch 2 application build. Native video shaders are separate and built locally.
