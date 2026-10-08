# xGPU on Linux (X11 first)

Standalone build: the library, the smallest example (E01) and a hands-free smoke test of the window backend.
It does not use the Windows-only parts of the main xGPU build (Install.bat, example.lionprj, shaderc.exe).

```
sudo apt install libvulkan-dev glslc libx11-dev clang cmake ninja-build
cmake -S Build/linux -B ~/xgpu-build -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build ~/xgpu-build
~/xgpu-build/xgpu_xlib_smoke      # window, surface, render, resize, keyboard, mouse, close - prints PASS/FAIL
~/xgpu-build/xgpu_e01             # E01: two windows with a textured cube (needs a display: X11, WSLg, XWayland)
```

## How the backends are laid out

| Folder | Role |
|---|---|
| `source/Details/System/Windows/` | Win32 backend (unchanged) |
| `source/Details/System/Linux/`   | Linux OS services shared by every Linux window backend: thread local storage, keyboard/mouse state, evdev key table |
| `source/Details/System/Xlib/`    | X11 window backend (Xlib + `VK_KHR_xlib_surface`). Also runs on WSLg / XWayland |
| `source/Details/System/Null/`    | no window system: headless (the default off Windows) |

* The backend is chosen in `xgpu_system.h`. X11 is **opt in**: a build defines `XGPU_SYSTEM_XLIB` (this CMake does); any other
  non-Windows build stays on the null backend, so existing consumers do not pick up a libX11 dependency.
* A backend provides `instance` (with `static getSurfaceExtensions()`, asked before the `VkInstance` is created) and `window`
  (with `CreateVulkanSurface()`); the Vulkan layer has one `#elif` for surface creation per backend and nothing else.
* A Wayland backend means a new `Wayland/` folder next to `Xlib/`, reusing `Linux/`.
