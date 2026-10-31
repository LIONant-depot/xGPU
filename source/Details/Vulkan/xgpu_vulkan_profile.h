#pragma once
// GPU zones for the profiler (xprofile / Tracy, tracy_plan.md T2): where xprofile is part of the build (XLION_PROFILE), Tracy's Vulkan
// context on the main queue - made when a frame begins with the profiler on (device::ProfileContext), its query results collected at
// the start of every frame - and the zones window/cmd_buffer::BeginGPUZone / EndGPUZone write. Without xprofile (tests, tools): nothing.
// The structures keep only opaque storage (a pointer, raw bytes): their layout is the same with or without it.
#if __has_include("xprofile.h")
    #include "xprofile.h"
#endif

#if defined(XLION_PROFILE) && XLION_PROFILE
    #define XGPU_PROFILE 1
    #include "tracy/TracyVulkan.hpp"
#else
    #define XGPU_PROFILE 0
#endif

namespace xgpu::vulkan
{
    // a window's open GPU zones (LIFO: each ends in the command buffer it began in); slots of raw bytes: tracy::VkCtxScope where profiling
    struct gpu_zone_stack
    {
        static constexpr int    max_v       = 32;
        static constexpr int    slot_size_v = 64;
        alignas(16) std::array<std::byte, max_v * slot_size_v> m_Memory {};
        std::array<bool, max_v> m_bLive     {};             // a real zone (false: the profiler was off when it began - nothing to end)
        int                     m_Count     = 0;
    };
}
