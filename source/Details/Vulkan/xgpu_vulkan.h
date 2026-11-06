
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#if defined(__linux__)
// LION patch (Linux/WSL only): Dozen (Mesa dzn) reports Vulkan 1.2 and implements
// synchronization2 only as the KHR entry points, which libvulkan does not export
// for linking. Capture the loader's core 1.3 symbols (llvmpipe), then on a 1.2
// device replace them with vkGetDeviceProcAddr("...KHR"). Windows is unchanged.
// Remove when Dozen advertises Vulkan 1.3.
inline PFN_vkQueueSubmit2        xgpu_disp_vkQueueSubmit2        = vkQueueSubmit2;
inline PFN_vkCmdPipelineBarrier2 xgpu_disp_vkCmdPipelineBarrier2 = vkCmdPipelineBarrier2;
inline VkResult xgpu_lion_vkQueueSubmit2(VkQueue q, uint32_t n, const VkSubmitInfo2* infos, VkFence fence)
{
    return xgpu_disp_vkQueueSubmit2(q, n, infos, fence);
}
inline void xgpu_lion_vkCmdPipelineBarrier2(VkCommandBuffer cmd, const VkDependencyInfo* dep)
{
    xgpu_disp_vkCmdPipelineBarrier2(cmd, dep);
}
#define vkQueueSubmit2        xgpu_lion_vkQueueSubmit2
#define vkCmdPipelineBarrier2 xgpu_lion_vkCmdPipelineBarrier2
#endif

#include <mutex>
#include <thread>
#include <unordered_map>
#include <deque>
#include <source_location>
#include <filesystem>
#include <fstream>

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
#include "xgpu_vulkan_profile.h"                 // the profiler's GPU zones (xprofile / Tracy, where it is part of the build)
#include "xgpu_vulkan_device.h"
#include "xgpu_vulkan_window.h"
#include "xgpu_vulkan_texture_table.h"

