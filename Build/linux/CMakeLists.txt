# Standalone Linux build of xGPU (library sources + the smallest example + the X11 backend smoke test).
# It needs none of the Windows-only parts of the main xGPU build (Install.bat, example.lionprj, shaderc.exe):
#
#   cmake -S Build/linux -B ~/xgpu-build -G Ninja -DCMAKE_CXX_COMPILER=clang++
#   cmake --build ~/xgpu-build
#   ~/xgpu-build/xgpu_xlib_smoke          # hands-free: window, surface, render, resize, input, close
#   ~/xgpu-build/xgpu_e01                 # the example, needs a display (WSLg, X11 or Wayland+XWayland)
#
# Packages (Ubuntu): libvulkan-dev glslc libx11-dev
cmake_minimum_required(VERSION 3.20)
project(xGPU_linux LANGUAGES CXX)

set(XGPU_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
cmake_path(NORMAL_PATH XGPU_ROOT)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

find_package(Vulkan REQUIRED)
find_package(X11    REQUIRED)
find_program(GLSLC glslc REQUIRED)

# draw_vert.h / draw_frag.h are #include'd inside the example: SPIR-V as a comma separated list of numbers
set(GEN "${CMAKE_BINARY_DIR}/sprv_shaders_h")
file(MAKE_DIRECTORY "${GEN}")
foreach(S frag vert)
  add_custom_command(OUTPUT "${GEN}/draw_${S}.h"
    COMMAND ${GLSLC} -fshader-stage=${S} "${XGPU_ROOT}/Bin/draw_${S}.glsl" -mfmt=num -o "${GEN}/draw_${S}.h"
    DEPENDS "${XGPU_ROOT}/Bin/draw_${S}.glsl")
  list(APPEND SHADER_HEADERS "${GEN}/draw_${S}.h")
endforeach()

# The library is one translation unit (xGPU.cpp pulls the OS backend and the Vulkan layer in)
add_library(xgpu STATIC "${XGPU_ROOT}/source/xGPU.cpp")
target_include_directories(xgpu PUBLIC "${XGPU_ROOT}" "${XGPU_ROOT}/source")
target_compile_definitions(xgpu PUBLIC NOMINMAX XGPU_SYSTEM_XLIB $<$<CONFIG:Debug>:_DEBUG>)   # XGPU_SYSTEM_XLIB: opt in to the X11 window backend
target_compile_options(xgpu PUBLIC -fms-extensions -fdeclspec -fbracket-depth=2048 -w)   # MSVC-style syntax the sources use, the SPIR-V initializer lists of the examples; third party noise off
target_link_libraries(xgpu PUBLIC Vulkan::Vulkan X11::X11 pthread ${CMAKE_DL_LIBS})

add_executable(xgpu_e01 xgpu_e01_main.cpp "${XGPU_ROOT}/source/Examples/E01_Initialize/E01_Initialize.cpp" ${SHADER_HEADERS})
target_include_directories(xgpu_e01 PRIVATE "${GEN}")
target_link_libraries(xgpu_e01 PRIVATE xgpu)

add_executable(xgpu_xlib_smoke xgpu_xlib_smoke.cpp)
target_link_libraries(xgpu_xlib_smoke PRIVATE xgpu)
