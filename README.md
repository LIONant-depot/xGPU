# xGPU
A modern low level graphics API
"Header only" (A header and a C++ file), and the core library does not require any dependencies. Everything you need is in the Src directory.

## Examples
Examples are not needed to run use xGPU in your code. However they provide useful tests to learn from.
### Prerequisites
+ [Visual Studio 2019](https://visualstudio.microsoft.com/downloads/)
+ [cmake](https://cmake.org/download/)
+ [vulkan](https://vulkan.lunarg.com/)
+ [RenderDoc](https://renderdoc.org/)

### Dependencies
To see the examples you will need to run a batch located: **Build/updateExamplesDependencies.bat**
it will gather and build the dependencies:
- [ImGuid](https://github.com/ocornut/imgui) 
- [xCore](https://gitlab.com/LIONant/xcore)
- [xprim_geom](https://github.com/LIONant-depot/xprim_geom)
- [xbmp_tools](https://github.com/LIONant-depot/xbmp_tools)
- [assimp](https://github.com/assimp/assimp)
- [Shaderc](https://github.com/google/shaderc)
