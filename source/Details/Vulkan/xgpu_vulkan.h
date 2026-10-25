
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <deque>
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

#include "../OffsetAllocator/offsetAllocator_inline.hpp"     // sebbbi/OffsetAllocator (MIT): the VRAM blocks' ranges
#include "xgpu_vulkan_vram.h"
#include "xgpu_vulkan_local_storage.h"
#include "xgpu_vulkan_instance.h"
#include "xgpu_vulkan_shader.h"
#include "xgpu_vulkan_texture.h"
#include "xgpu_vulkan_vertex_descriptor.h"
#include "xgpu_vulkan_buffer.h"
#include "xgpu_vulkan_pipeline.h"
#include "xgpu_vulkan_pipeline_instance.h"
#include "xgpu_vulkan_renderpass.h"
#include "xgpu_vulkan_timestamps.h"
#include "xgpu_vulkan_compute.h"
#include "xgpu_vulkan_descriptor_pool_ring.h"
#include "xgpu_vulkan_upload.h"
#include "xgpu_vulkan_device.h"
#include "xgpu_vulkan_window.h"
#include "xgpu_vulkan_texture_table.h"

