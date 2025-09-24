# xGPU: Revolutionize Your Graphics Rendering with a Modern Low-Level API!

Elevate your graphics programming with **xGPU** – a sleek, high-performance C++ library for direct hardware access and blazing-fast rendering. Ditch bloated frameworks and embrace a lightweight, header-only design that simplifies window management, pipeline setup, and buffer handling. With built-in support for Vulkan, multi-window rendering, and zero-overhead abstractions, xGPU empowers developers to craft efficient, cross-compatible applications effortlessly!

## Key Features

- **Header-Only Simplicity**: Just include `xGPU.h` and `xGPU.cpp` – core library integrates seamlessly without build hassles.
- **Zero Dependencies for Core**: No external libs needed; everything's self-contained in the `Src` directory.
- **Modern Low-Level Control**: Direct access to graphics devices, shaders, pipelines, textures, and buffers for optimal performance.
- **Multi-Window Support**: Easily create and manage multiple windows with independent rendering.
- **Debug and Logging Tools**: Enable debug mode, RenderDoc integration, and custom error/warning callbacks for rapid development.
- **Flexible Shaders**: Load raw shader data for vertex/fragment stages with push constants and uniform support.
- **Efficient Buffers**: Vertex and index buffers with memory mapping for quick data uploads.
- **Texture Handling**: Simple creation and binding for 2D textures with mip levels.
- **Pipeline Management**: Define vertex descriptors, samplers, and instances for customizable rendering pipelines.
- **Cross-Platform Ready**: Works on Windows (extendable to others) with Vulkan backend.
- **MIT License**: Free, open-source, and community-friendly.
- **Easy Integration**: Drop into any C++ project; examples demonstrate real-world usage.
- **Unit Tests & Examples**: Explore code samples and tests in the repo for quick learning.

## Dependencies

- **Core Library**: None – fully standalone.
- **Examples Only**:
  - [ImGui](https://github.com/ocornut/imgui): For UI in demos.
  - [xCore](https://gitlab.com/LIONant/xcore): Utility functions.
  - [xprim_geom](https://github.com/LIONant-depot/xprim_geom): Geometry primitives.
  - [xbmp_tools](https://github.com/LIONant-depot/xbmp_tools): Bitmap handling.
  - [assimp](https://github.com/assimp/assimp): Model loading.
  - [Shaderc](https://github.com/google/shaderc): Shader compilation.
  - etc...

## Prerequisites for Building and Running Examples

- [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/): Required for compiling on Windows.
- [CMake](https://cmake.org/download/): For generating build files (For the examples only).
- [Vulkan SDK](https://vulkan.lunarg.com/): Essential for Vulkan backend support.
- [RenderDoc](https://renderdoc.org/): For graphics debugging and capture.

## Getting Started

- Clone the repo: `git clone https://github.com/LIONant-depot/xGPU`.
- For core use: Add `Src/xGPU.h` and `Src/xGPU.cpp` to your project.
- For examples: Run `Build/CreateProject.bat` to fetch and build dependencies.

## Code Example

```cpp
#include "xGPU.h"
#include <iostream>

static void DebugMessage(std::string_view View) { printf("%s\n", View.data()); }

struct draw_vert {
    float m_X, m_Y, m_Z;
    float m_U, m_V;
    std::uint32_t m_Color;
};

int main() {
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, {.m_bDebugMode = true, .m_pLogErrorFunc = DebugMessage, .m_pLogWarning = DebugMessage}); Err) return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err) return xgpu::getErrorInt(Err);

    xgpu::window Window;
    if (auto Err = Device.Create(Window, {}); Err) return xgpu::getErrorInt(Err);

    // Vertex Descriptor
    xgpu::vertex_descriptor VertexDescriptor;
    auto Attributes = std::array{
        xgpu::vertex_descriptor::attribute{.m_Offset = offsetof(draw_vert, m_X), .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D},
        xgpu::vertex_descriptor::attribute{.m_Offset = offsetof(draw_vert, m_U), .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D},
        xgpu::vertex_descriptor::attribute{.m_Offset = offsetof(draw_vert, m_Color), .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED}
    };
    if (auto Err = Device.Create(VertexDescriptor, {.m_VertexSize = sizeof(draw_vert), .m_Attributes = Attributes}); Err) return xgpu::getErrorInt(Err);

    // Shaders and Pipeline (assume draw_vert.h and draw_frag.h contain raw shader data)
    xgpu::shader VertexShader, FragmentShader;
    // ... Create shaders with raw data ...

    xgpu::pipeline Pipeline;
    // ... Create pipeline with shaders, vertex descriptor, etc. ...

    // Buffers
    xgpu::buffer VertexBuffer, IndexBuffer;
    // ... Create and map vertex/index data for a cube ...

    while (Instance.ProcessInputEvents()) {
        if (Window.BeginRendering()) continue;
        auto CmdBuffer = Window.getCmdBuffer();
        // Set pipeline, buffers, push constants, draw ...
        Window.PageFlip();
    }

    return 0;
}
```

Dive into xGPU today – star, fork, and contribute to power your next-gen graphics projects! 🚀