
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#if defined(__linux__)
// LION patch (WSL + Dozen only): Dozen (Mesa dzn) reports Vulkan 1.2 and implements
// synchronization2 only as the KHR entry points, which libvulkan does not export
// for linking. These start as the loader's core 1.3 symbols (native Linux, llvmpipe).
// CreateGraphicsDevice replaces them with vkGetDeviceProcAddr("...KHR") only for a
// WSL Dozen device. Native Linux and Windows use the standard Vulkan 1.3 path.
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

#if defined(__linux__)
#include <cctype>
#include <cstdlib>
#include <string>

// LION patch (WSL + Dozen only): detect the WSL environment and Mesa's Dozen ICD.
// Native Linux and Windows use the standard Vulkan 1.3 path.
// Remove when Dozen advertises Vulkan 1.3.
inline bool xgpu_RunningUnderWSL() noexcept
{
    static const bool bWSL = []() -> bool
    {
        if( const char* n = std::getenv("WSL_DISTRO_NAME"); n && n[0] ) return true;
        auto FileMentionsWSL = []( const char* Path ) -> bool
        {
            std::ifstream In(Path);
            if( !In ) return false;
            std::string Line;
            if( !std::getline(In, Line) ) return false;
            for( char& C : Line ) C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
            return Line.find("microsoft") != std::string::npos || Line.find("wsl") != std::string::npos;
        };
        return FileMentionsWSL("/proc/sys/kernel/osrelease") || FileMentionsWSL("/proc/version");
    }();
    return bWSL;
}

inline bool xgpu_PhysicalDeviceIsMesaDozen( VkPhysicalDevice PhysicalDevice ) noexcept
{
    VkPhysicalDeviceDriverProperties Driver{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
    VkPhysicalDeviceProperties2      Props2{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &Driver };
    vkGetPhysicalDeviceProperties2(PhysicalDevice, &Props2);
    return Driver.driverID == VK_DRIVER_ID_MESA_DOZEN;
}

inline bool xgpu_DeviceIsDozenCompat( VkPhysicalDevice PhysicalDevice ) noexcept
{
    if( !xgpu_RunningUnderWSL() ) return false;
    VkPhysicalDeviceProperties Props{};
    vkGetPhysicalDeviceProperties(PhysicalDevice, &Props);
    if( Props.apiVersion >= VK_API_VERSION_1_3 ) return false;
    return xgpu_PhysicalDeviceIsMesaDozen(PhysicalDevice);
}
#endif

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
#include "xgpu_vulkan_frame_map.h"
#include "xgpu_vulkan_window.h"
#include "xgpu_vulkan_texture_table.h"

