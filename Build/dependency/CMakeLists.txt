include(${CMAKE_BINARY_DIR}/_deps/xcmake_tools/Common.cmake)

## These are not true dependencies but they are require for the tools
FetchAndPopulate("https://github.com/LIONant-depot/xmath.git" "main")


DefineInterfaceComponent(xGPU "dependencies"
  "source/xGPU.cpp"
  "source/xGPU.h"
  "source/xgpu_buffer.h"
  "source/xgpu_cmd_buffer.h"
  "source/xgpu_device.h"
  "source/xgpu_instance.h"
  "source/xgpu_keyboard.h"
  "source/xgpu_mouse.h"
  "source/xgpu_pipeline.h"
  "source/xgpu_pipeline_instance.h"
  "source/xgpu_renderpass.h"
  "source/xgpu_shader.h"
  "source/xgpu_texture.h"
  "source/xgpu_vertex_descriptor.h"
  "source/xgpu_window.h"
  "**Implementation"
  "source/details/xgpu_cmd_buffer_inline.h"
  "source/details/xgpu_device_inline.h"
  "source/details/xgpu_instance_inline.h"
  "source/details/xgpu_keyboard_inline.h"
  "source/details/xgpu_mouse_inline.h"
  "source/details/xgpu_pipeline_inline.h"
  "source/details/xgpu_pipeline_instance_inline.h"
  "source/details/xgpu_renderpass_inline.h"
  "source/details/xgpu_shader_inline.h"
  "source/details/xgpu_texture_inline.h"
  "source/details/xgpu_vertex_descriptor_inline.h"
  "source/details/xgpu_window_inline.h"
  "README.md"
)
if(xGPU_CREATED)
  AppendComponentIncludes(xGPU "$ENV{VULKAN_SDK}/Include" )
  AppendComponentLinkerPaths(xGPU "$ENV{VULKAN_SDK}" )
endif()