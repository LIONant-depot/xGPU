
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <source_location>

#ifdef CreateWindow
#undef CreateWindow
#endif

#include "../System/xgpu_system.h"

namespace xgpu::vulkan
{
    struct device;
    struct frame;
    struct frame_semaphores;
}

#include "xgpu_vulkan_local_storage.h"
#include "xgpu_vulkan_instance.h"
#include "xgpu_vulkan_shader.h"
#include "xgpu_vulkan_texture.h"
#include "xgpu_vulkan_vertex_descriptor.h"
#include "xgpu_vulkan_buffer.h"
#include "xgpu_vulkan_pipeline.h"
#include "xgpu_vulkan_pipeline_instance.h"
#include "xgpu_vulkan_renderpass.h"
#include "xgpu_vulkan_device.h"
#include "xgpu_vulkan_window.h"

