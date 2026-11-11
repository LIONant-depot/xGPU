
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>
#include <atomic>
#include <mutex>
#include <unordered_map>

// (C30 diagnostic, XGPU_VK_COUNT: the object and descriptor calls counted - creates, destroys, frees, resets, updates - printed every 600
// device frames by device::DeathMarch: which of them happen during play, while a frame is in flight)
namespace xgpu_vkcount
{
    enum id { CREATE_PIPELINE, DESTROY_PIPELINE, CREATE_IMAGE, DESTROY_IMAGE, CREATE_VIEW, DESTROY_VIEW, CREATE_BUFFER, DESTROY_BUFFER
            , ALLOC_MEMORY, FREE_MEMORY, ALLOC_SETS, FREE_SETS, RESET_POOL, UPDATE_SETS, CREATE_SAMPLER, CREATE_FRAMEBUFFER, DESTROY_FRAMEBUFFER
            , CREATE_SHADER, DESTROY_SHADER, CREATE_POOL, DESTROY_POOL, BIND_SET0, BIND_SET1, BIND_SET2, BIND_SET3, BIND_PIPELINE, DRAWS, count_v };
    inline std::atomic<std::uint64_t> g_Count[count_v]{};
    inline const char* Name(int i) noexcept
    {
        static const char* N[count_v] = { "CreateGraphicsPipelines", "DestroyPipeline", "CreateImage", "DestroyImage", "CreateImageView", "DestroyImageView", "CreateBuffer", "DestroyBuffer"
                                        , "AllocateMemory", "FreeMemory", "AllocateDescriptorSets", "FreeDescriptorSets", "ResetDescriptorPool", "UpdateDescriptorSets", "CreateSampler", "CreateFramebuffer", "DestroyFramebuffer"
                                        , "CreateShaderModule", "DestroyShaderModule", "CreateDescriptorPool", "DestroyDescriptorPool", "BindSet0", "BindSet1", "BindSet2", "BindSet3", "BindPipeline", "Draws" };
        return N[i];
    }
    inline VkResult CreateGraphicsPipelines(VkDevice d, VkPipelineCache c, uint32_t n, const VkGraphicsPipelineCreateInfo* p, const VkAllocationCallbacks* a, VkPipeline* o) { ++g_Count[CREATE_PIPELINE]; return ::vkCreateGraphicsPipelines(d, c, n, p, a, o); }
    inline void     DestroyPipeline(VkDevice d, VkPipeline x, const VkAllocationCallbacks* a) { ++g_Count[DESTROY_PIPELINE]; ::vkDestroyPipeline(d, x, a); }
    inline VkResult CreateImage(VkDevice d, const VkImageCreateInfo* p, const VkAllocationCallbacks* a, VkImage* o) { ++g_Count[CREATE_IMAGE]; return ::vkCreateImage(d, p, a, o); }
    inline void     DestroyImage(VkDevice d, VkImage x, const VkAllocationCallbacks* a) { ++g_Count[DESTROY_IMAGE]; ::vkDestroyImage(d, x, a); }
    inline VkResult CreateImageView(VkDevice d, const VkImageViewCreateInfo* p, const VkAllocationCallbacks* a, VkImageView* o) { ++g_Count[CREATE_VIEW]; return ::vkCreateImageView(d, p, a, o); }
    inline void     DestroyImageView(VkDevice d, VkImageView x, const VkAllocationCallbacks* a) { ++g_Count[DESTROY_VIEW]; ::vkDestroyImageView(d, x, a); }
    inline VkResult CreateBuffer(VkDevice d, const VkBufferCreateInfo* p, const VkAllocationCallbacks* a, VkBuffer* o) { ++g_Count[CREATE_BUFFER]; return ::vkCreateBuffer(d, p, a, o); }
    inline void     DestroyBuffer(VkDevice d, VkBuffer x, const VkAllocationCallbacks* a) { ++g_Count[DESTROY_BUFFER]; ::vkDestroyBuffer(d, x, a); }
    inline VkResult AllocateMemory(VkDevice d, const VkMemoryAllocateInfo* p, const VkAllocationCallbacks* a, VkDeviceMemory* o) { ++g_Count[ALLOC_MEMORY]; return ::vkAllocateMemory(d, p, a, o); }
    inline void     FreeMemory(VkDevice d, VkDeviceMemory x, const VkAllocationCallbacks* a) { ++g_Count[FREE_MEMORY]; ::vkFreeMemory(d, x, a); }
    inline VkResult AllocateDescriptorSets(VkDevice d, const VkDescriptorSetAllocateInfo* p, VkDescriptorSet* o) { ++g_Count[ALLOC_SETS]; return ::vkAllocateDescriptorSets(d, p, o); }
    inline VkResult FreeDescriptorSets(VkDevice d, VkDescriptorPool pl, uint32_t n, const VkDescriptorSet* s) { ++g_Count[FREE_SETS]; return ::vkFreeDescriptorSets(d, pl, n, s); }
    inline VkResult ResetDescriptorPool(VkDevice d, VkDescriptorPool pl, VkDescriptorPoolResetFlags f) { ++g_Count[RESET_POOL]; return ::vkResetDescriptorPool(d, pl, f); }
    inline void     UpdateDescriptorSets(VkDevice d, uint32_t n, const VkWriteDescriptorSet* w, uint32_t m, const VkCopyDescriptorSet* c) { ++g_Count[UPDATE_SETS]; ::vkUpdateDescriptorSets(d, n, w, m, c); }
    inline VkResult CreateSampler(VkDevice d, const VkSamplerCreateInfo* p, const VkAllocationCallbacks* a, VkSampler* o) { ++g_Count[CREATE_SAMPLER]; return ::vkCreateSampler(d, p, a, o); }
    inline VkResult CreateFramebuffer(VkDevice d, const VkFramebufferCreateInfo* p, const VkAllocationCallbacks* a, VkFramebuffer* o) { ++g_Count[CREATE_FRAMEBUFFER]; return ::vkCreateFramebuffer(d, p, a, o); }
    inline void     DestroyFramebuffer(VkDevice d, VkFramebuffer x, const VkAllocationCallbacks* a) { ++g_Count[DESTROY_FRAMEBUFFER]; ::vkDestroyFramebuffer(d, x, a); }
    inline VkResult CreateShaderModule(VkDevice d, const VkShaderModuleCreateInfo* p, const VkAllocationCallbacks* a, VkShaderModule* o) { ++g_Count[CREATE_SHADER]; return ::vkCreateShaderModule(d, p, a, o); }
    inline void     DestroyShaderModule(VkDevice d, VkShaderModule x, const VkAllocationCallbacks* a) { ++g_Count[DESTROY_SHADER]; ::vkDestroyShaderModule(d, x, a); }
    inline VkResult CreateDescriptorPool(VkDevice d, const VkDescriptorPoolCreateInfo* p, const VkAllocationCallbacks* a, VkDescriptorPool* o) { ++g_Count[CREATE_POOL]; return ::vkCreateDescriptorPool(d, p, a, o); }
    inline void     DestroyDescriptorPool(VkDevice d, VkDescriptorPool x, const VkAllocationCallbacks* a) { ++g_Count[DESTROY_POOL]; ::vkDestroyDescriptorPool(d, x, a); }
    inline void     CmdBindDescriptorSets(VkCommandBuffer c, VkPipelineBindPoint bp, VkPipelineLayout l, uint32_t first, uint32_t n, const VkDescriptorSet* s, uint32_t nd, const uint32_t* d)
    { if( bp == VK_PIPELINE_BIND_POINT_GRAPHICS ) for( uint32_t i = first; i < first + n && i < 4; ++i ) ++g_Count[BIND_SET0 + int(i)]; ::vkCmdBindDescriptorSets(c, bp, l, first, n, s, nd, d); }
    // (C30, XGPU_PIPELINE_STATS + XGPU_VK_COUNT: each pipeline's shader code bytes, from its statistics; the distinct pipelines a device frame binds)
    inline std::mutex                                   g_CodeLock;
    inline std::unordered_map<VkPipeline, std::uint64_t> g_Code;
    inline std::unordered_map<VkPipeline, int>           g_FrameBound;
    inline std::unordered_map<VkPipeline, std::string>   g_Label;
    inline void     CmdBindPipeline(VkCommandBuffer c, VkPipelineBindPoint bp, VkPipeline p) { if( bp == VK_PIPELINE_BIND_POINT_GRAPHICS ) ++g_Count[BIND_PIPELINE]; { std::lock_guard Lk(g_CodeLock); ++g_FrameBound[p]; } ::vkCmdBindPipeline(c, bp, p); }
    inline void     CmdDrawIndexed(VkCommandBuffer c, uint32_t a, uint32_t b, uint32_t e, int32_t f, uint32_t g) { ++g_Count[DRAWS]; ::vkCmdDrawIndexed(c, a, b, e, f, g); }
    inline void     CmdDrawIndexedIndirect(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, uint32_t n, uint32_t s) { ++g_Count[DRAWS]; ::vkCmdDrawIndexedIndirect(c, b, o, n, s); }
    inline void     CmdDrawIndexedIndirectCount(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, VkBuffer cb, VkDeviceSize co, uint32_t m, uint32_t s) { ++g_Count[DRAWS]; ::vkCmdDrawIndexedIndirectCount(c, b, o, cb, co, m, s); }
    inline void     CmdDraw(VkCommandBuffer c, uint32_t a, uint32_t b, uint32_t e, uint32_t f) { ++g_Count[DRAWS]; ::vkCmdDraw(c, a, b, e, f); }
}
#define vkCmdBindDescriptorSets         xgpu_vkcount::CmdBindDescriptorSets
#define vkCmdBindPipeline               xgpu_vkcount::CmdBindPipeline
#define vkCmdDrawIndexed                xgpu_vkcount::CmdDrawIndexed
#define vkCmdDrawIndexedIndirect        xgpu_vkcount::CmdDrawIndexedIndirect
#define vkCmdDrawIndexedIndirectCount   xgpu_vkcount::CmdDrawIndexedIndirectCount
#define vkCmdDraw                       xgpu_vkcount::CmdDraw
#define vkCreateGraphicsPipelines   xgpu_vkcount::CreateGraphicsPipelines
#define vkDestroyPipeline           xgpu_vkcount::DestroyPipeline
#define vkCreateImage               xgpu_vkcount::CreateImage
#define vkDestroyImage              xgpu_vkcount::DestroyImage
#define vkCreateImageView           xgpu_vkcount::CreateImageView
#define vkDestroyImageView          xgpu_vkcount::DestroyImageView
#define vkCreateBuffer              xgpu_vkcount::CreateBuffer
#define vkDestroyBuffer             xgpu_vkcount::DestroyBuffer
#define vkAllocateMemory            xgpu_vkcount::AllocateMemory
#define vkFreeMemory                xgpu_vkcount::FreeMemory
#define vkAllocateDescriptorSets    xgpu_vkcount::AllocateDescriptorSets
#define vkFreeDescriptorSets        xgpu_vkcount::FreeDescriptorSets
#define vkResetDescriptorPool       xgpu_vkcount::ResetDescriptorPool
#define vkUpdateDescriptorSets      xgpu_vkcount::UpdateDescriptorSets
#define vkCreateSampler             xgpu_vkcount::CreateSampler
#define vkCreateFramebuffer         xgpu_vkcount::CreateFramebuffer
#define vkDestroyFramebuffer        xgpu_vkcount::DestroyFramebuffer
#define vkCreateShaderModule        xgpu_vkcount::CreateShaderModule
#define vkDestroyShaderModule       xgpu_vkcount::DestroyShaderModule
#define vkCreateDescriptorPool      xgpu_vkcount::CreateDescriptorPool
#define vkDestroyDescriptorPool     xgpu_vkcount::DestroyDescriptorPool

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

