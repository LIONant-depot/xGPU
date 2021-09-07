# xGPU
A modern low level graphics API
"Header only" (A header and a C++ file), and the core library does not require any dependencies. Everything you need is in the Src directory.
## Prerequisites
+ [Visual Studio 2019](https://visualstudio.microsoft.com/downloads/)
+ [cmake](https://cmake.org/download/)
+ [vulkan](https://vulkan.lunarg.com/)

## Examples require dependencies
Run the batch located: **Build/updateExamplesDependencies.bat**
it will gather and build the following:
- [ImGuid](https://github.com/ocornut/imgui) 
- [xCore](https://gitlab.com/LIONant/xcore)
- [xprim_geom](https://github.com/LIONant-depot/xprim_geom)
- [xbmp_tools](https://github.com/LIONant-depot/xbmp_tools)
- [assimp](https://github.com/assimp/assimp)
- [Shaderc](https://github.com/google/shaderc)
