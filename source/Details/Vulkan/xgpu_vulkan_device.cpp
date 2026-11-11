#include <mutex>
#include <unordered_set>
#include <chrono>
#include "../xgpu_env.h"
#if defined(_WIN32)
#include <cstdio>
#include <string>
#include <vector>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace xgpu::vulkan
{
#if defined(_WIN32)
    // NVIDIA Nsight Aftermath (C30), XGPU_AFTERMATH=<folder holding GFSDK_Aftermath_Lib.x64.dll>, measurement: when the GPU times out the driver hands over a crash
    // dump, written there with its JSON decode (the active shaders, the fault, the device's state). The library is loaded by name: nothing links or ships with it
    namespace aftermath
    {
        constexpr std::uint32_t version_v = 0x000021B, watch_vulkan_v = 0x2, decode_all_v = 0x3FFF, format_utf8_v = 0x2;
        using dump_cb   = void(*)(const void*, std::uint32_t, void*);
        using enable_fn = int(*)(std::uint32_t, std::uint32_t, std::uint32_t, dump_cb, void*, void*, void*, void*);
        using create_fn = int(*)(std::uint32_t, const void*, std::uint32_t, void**);
        using destroy_fn= int(*)(void*);
        using json_fn   = int(*)(void*, std::uint32_t, std::uint32_t, void*, void*, void*, void*, std::uint32_t*);
        using getjson_fn= int(*)(void*, std::uint32_t, char*);
        static HMODULE      s_Lib = nullptr;
        static std::string  s_Folder;

        static void OnDump(const void* pDump, std::uint32_t Size, void*) noexcept
        {
            static int s_n = 0;
            const std::string Base = s_Folder + "\\xlion_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(s_n++);
            if( FILE* f = std::fopen((Base + ".nv-gpudmp").c_str(), "wb") ) { std::fwrite(pDump, 1, Size, f); std::fclose(f); }
            std::printf("[xGPU] AFTERMATH dump written: %s.nv-gpudmp (%u bytes)\n", Base.c_str(), Size); std::fflush(stdout);
            const auto Create  = reinterpret_cast<create_fn>(GetProcAddress(s_Lib, "GFSDK_Aftermath_GpuCrashDump_CreateDecoder"));
            const auto Destroy = reinterpret_cast<destroy_fn>(GetProcAddress(s_Lib, "GFSDK_Aftermath_GpuCrashDump_DestroyDecoder"));
            const auto Json    = reinterpret_cast<json_fn>(GetProcAddress(s_Lib, "GFSDK_Aftermath_GpuCrashDump_GenerateJSON"));
            const auto GetJson = reinterpret_cast<getjson_fn>(GetProcAddress(s_Lib, "GFSDK_Aftermath_GpuCrashDump_GetJSON"));
            void* pDecoder = nullptr;
            if( !Create || !Destroy || !Json || !GetJson || Create(version_v, pDump, Size, &pDecoder) != 1 ) { std::printf("[xGPU] AFTERMATH no decoder\n"); std::fflush(stdout); return; }
            std::uint32_t JsonSize = 0;
            if( Json(pDecoder, decode_all_v, format_utf8_v, nullptr, nullptr, nullptr, nullptr, &JsonSize) == 1 && JsonSize )
            {
                std::vector<char> Text(JsonSize);
                if( GetJson(pDecoder, JsonSize, Text.data()) == 1 )
                    if( FILE* f = std::fopen((Base + ".json").c_str(), "wb") ) { std::fwrite(Text.data(), 1, JsonSize ? JsonSize - 1 : 0, f); std::fclose(f); }
            }
            Destroy(pDecoder);
            std::printf("[xGPU] AFTERMATH decoded: %s.json\n", Base.c_str()); std::fflush(stdout);
        }

        // Before the device is made; true: the device asks for the diagnostics
        static bool Enable(void) noexcept
        {
            const char* pFolder = xgpu::details::Env("XGPU_AFTERMATH");
            if( pFolder == nullptr ) return false;
            s_Folder = pFolder;
            s_Lib = LoadLibraryA((s_Folder + "\\GFSDK_Aftermath_Lib.x64.dll").c_str());
            const auto Fn = s_Lib ? reinterpret_cast<enable_fn>(GetProcAddress(s_Lib, "GFSDK_Aftermath_EnableGpuCrashDumps")) : nullptr;
            const int  R  = Fn ? Fn(version_v, watch_vulkan_v, 0u, &OnDump, nullptr, nullptr, nullptr, nullptr) : 0;
            std::printf("[xGPU] AFTERMATH %s (result 0x%x)\n", R == 1 ? "on" : "could not start", unsigned(R)); std::fflush(stdout);
            return R == 1;
        }
    }
#endif

    //----------------------------------------------------------------------------------------------------------

    device::~device(void) noexcept
    {
        for (auto Pool : m_AdditionalSamplerPools)
        {
            if (Pool != VK_NULL_HANDLE)
                vkDestroyDescriptorPool(m_VKDevice, Pool, m_Instance->m_pVKAllocator);
        }
        m_AdditionalSamplerPools.clear();
    }

    VkDescriptorPool device::CreateSamplerDescriptorPool(void) noexcept
    {
        // One COMBINED_IMAGE_SAMPLER set per pipeline instance (up to 16 texture slots;
        // xlionrender lit pipelines use 13). Descriptors must be maxSets * slots: a pool of
        // 1024 descriptors and 1024 maxSets only fits 1-sampler sets. Dozen tracks this
        // strictly (and adds per-set alignment in its bindless path). A single pool is still
        // not enough for a full project, so AllocateSamplerDescriptorSet creates another
        // when this one returns OUT_OF_POOL_MEMORY or FRAGMENTED_POOL. Required on every
        // Vulkan device, not only Dozen.
        constexpr auto max_sets_per_pool_v             = 4096u;
        constexpr auto max_samplers_per_set_v          = 16u;
        constexpr auto combined_descriptors_per_pool_v = max_sets_per_pool_v * max_samplers_per_set_v;

        const VkDescriptorPoolSize PoolSizes[] =
        {   { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, combined_descriptors_per_pool_v }
        };

        const VkDescriptorPoolCreateInfo PoolCreateInfo =
        { .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
        , .flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
        , .maxSets       = max_sets_per_pool_v
        , .poolSizeCount = 1
        , .pPoolSizes    = PoolSizes
        };

        VkDescriptorPool Pool = VK_NULL_HANDLE;
        if (auto VKErr = vkCreateDescriptorPool(m_VKDevice, &PoolCreateInfo, m_Instance->m_pVKAllocator, &Pool); VKErr)
        {
            m_Instance->ReportError(VKErr, "Fail to create a Descriptor Pool");
            return VK_NULL_HANDLE;
        }
        return Pool;
    }

    VkResult device::AllocateSamplerDescriptorSet(VkDescriptorSetLayout Layout, VkDescriptorSet& OutSet, VkDescriptorPool& OutPool) noexcept
    {
        // Caller holds m_LockedVKDescriptorPool.
        constexpr std::size_t max_sampler_descriptor_pools_v = 16; // first + 15 additional

        const auto TryOne = [&](VkDescriptorPool Pool) -> VkResult
        {
            OutSet = VK_NULL_HANDLE;
            const VkDescriptorSetAllocateInfo Alloc
            { .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
            , .descriptorPool     = Pool
            , .descriptorSetCount = 1
            , .pSetLayouts        = &Layout
            };
            const VkResult Err = vkAllocateDescriptorSets(m_VKDevice, &Alloc, &OutSet);
            if (Err == VK_SUCCESS)
                OutPool = Pool;
            return Err;
        };

        // Newest extra pool first so a full earlier pool is not probed (and logged) on every later allocate.
        VkResult Err = VK_ERROR_OUT_OF_POOL_MEMORY;
        for (auto It = m_AdditionalSamplerPools.rbegin(); It != m_AdditionalSamplerPools.rend(); ++It)
        {
            Err = TryOne(*It);
            if (Err == VK_SUCCESS)
                return VK_SUCCESS;
            if (Err != VK_ERROR_OUT_OF_POOL_MEMORY && Err != VK_ERROR_FRAGMENTED_POOL)
                return Err;
        }

        Err = TryOne(m_LockedVKDescriptorPool.get());
        if (Err == VK_SUCCESS)
            return VK_SUCCESS;
        if (Err != VK_ERROR_OUT_OF_POOL_MEMORY && Err != VK_ERROR_FRAGMENTED_POOL)
            return Err;

        if (1 + m_AdditionalSamplerPools.size() >= max_sampler_descriptor_pools_v)
            return VK_ERROR_OUT_OF_POOL_MEMORY;

        const VkDescriptorPool NewPool = CreateSamplerDescriptorPool();
        if (NewPool == VK_NULL_HANDLE)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        m_AdditionalSamplerPools.push_back(NewPool);
        return TryOne(NewPool);
    }

    //----------------------------------------------------------------------------------------------------------

    xgpu::device::error* CreateGraphicsDevice
    ( device&                                       Device
    , const bool                                    enableValidation
    , const std::vector<VkQueueFamilyProperties>&   DeviceProperties 
    ) noexcept
    {
        //
        // Lets find a transfer queue... because we are going to let assets transfer at lighting speed
        // (So async transfers)
        for( auto i=0u; i<DeviceProperties.size(); ++i )
        {
            // We want to find a queue that is not compute, or graphics
            // we just need to transfer
            if(    (DeviceProperties[i].queueFlags & VK_QUEUE_TRANSFER_BIT) 
                && ( Device.m_MainQueueIndex != i )
              )
            {
                Device.m_TransferQueueIndex = i;
                break;
            }
        }

        // A device with a single queue family (software drivers such as lavapipe, some integrated GPUs): the transfers share the main queue
        // (XGPU_ONE_QUEUE_FAMILY: the same on any device, to test that path)
        if( Device.m_TransferQueueIndex == 0xffffffff || xgpu::details::Env("XGPU_ONE_QUEUE_FAMILY") ) Device.m_TransferQueueIndex = Device.m_MainQueueIndex;
        const bool bOwnTransferFamily = Device.m_TransferQueueIndex != Device.m_MainQueueIndex;

        //
        // Prepare our queues
        //
        static const std::array     queuePriorities    = {    0.0f };
        // the main family's second queue: dispatches beside the frame (window::BeginAsyncCompute); one family, no ownership to move
        static const std::array     mainPriorities     = {    0.0f, 0.0f };
        Device.m_bAsyncCompute = DeviceProperties[Device.m_MainQueueIndex].queueCount >= 2 && xgpu::details::Env("XGPU_NO_ASYNC") == nullptr;
        auto queueCreateInfo = std::array
        {   VkDeviceQueueCreateInfo 
            {
                .sType              = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO
            ,   .queueFamilyIndex   = Device.m_MainQueueIndex
            ,   .queueCount         = Device.m_bAsyncCompute ? 2u : 1u
            ,   .pQueuePriorities   = mainPriorities.data()
            }
        ,   VkDeviceQueueCreateInfo
            {
                .sType              = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO
            ,   .queueFamilyIndex   = Device.m_TransferQueueIndex
            ,   .queueCount         = static_cast<std::uint32_t>(queuePriorities.size())
            ,   .pQueuePriorities   = queuePriorities.data()
            }
        };

        //
        // Create device now
        //
        VkPhysicalDeviceFeatures Features{};
        vkGetPhysicalDeviceFeatures( Device.m_VKPhysicalDevice, &Features );
        Features.shaderClipDistance = true;
        Features.shaderCullDistance = true;
        Features.samplerAnisotropy  = true;

        // Vulkan 1.3 is the minimum on Windows (instance.cpp filters the devices): timeline semaphores and synchronization2 are core and
        // always on. Host query reset lets vulkan::timestamps recycle its queries from the CPU, so a timestamp can be written
        // anywhere in a frame, including inside a render pass (vkCmdResetQueryPool can't be).
        // Bindless (descriptor indexing): one global array of sampled images and one of samplers, indexed by texture_id /
        // sampler_id, partially bound and updated while bound. Every feature it needs, or the device uses the per-material sets
        // (XGPU_NO_BINDLESS forces those, to test them)
        VkPhysicalDeviceVulkan13Features Has13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        VkPhysicalDeviceVulkan12Features Has12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &Has13 };
#if defined(__linux__)
        // LION patch (WSL + Dozen only): Dozen (Mesa dzn) reports Vulkan 1.2 and does not fill
        // VkPhysicalDeviceVulkan13Features. Query the same features via their KHR structs.
        // Native Linux and Windows use the standard Vulkan 1.3 path.
        // Remove when Dozen advertises Vulkan 1.3.
        VkPhysicalDeviceSynchronization2FeaturesKHR HasSync2{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR, .pNext = &Has13 };
        VkPhysicalDeviceDynamicRenderingFeaturesKHR HasDynRend{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR, .pNext = &HasSync2 };
        Device.m_bDozenCompat = xgpu_DeviceIsDozenCompat(Device.m_VKPhysicalDevice);
        Device.m_bVertexBindingStrideDynamic = !Device.m_bDozenCompat;
        if( Device.m_bDozenCompat )
            Has12.pNext = &HasDynRend;
#endif
        {
            VkPhysicalDeviceFeatures2 Features2{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &Has12 };
            vkGetPhysicalDeviceFeatures2( Device.m_VKPhysicalDevice, &Features2 );
        }
        const VkBool32 hasSync2 =
#if defined(__linux__)
            Device.m_bDozenCompat ? HasSync2.synchronization2 :
#endif
            Has13.synchronization2;
        const VkBool32 hasDynRend =
#if defined(__linux__)
            Device.m_bDozenCompat ? HasDynRend.dynamicRendering :
#endif
            Has13.dynamicRendering;
        if( !Has12.timelineSemaphore || !hasSync2 || !hasDynRend )
        {
            Device.m_Instance->ReportError("xGPU needs a Vulkan 1.3 device (timeline semaphores, synchronization2, dynamic rendering)");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "xGPU needs a Vulkan 1.3 device (timeline semaphores, synchronization2, dynamic rendering)");
        }
        Device.m_bHostQueryReset = Has12.hostQueryReset == VK_TRUE;
        Device.m_bBindless       = Has12.runtimeDescriptorArray && Has12.descriptorBindingPartiallyBound
                                && Has12.descriptorBindingSampledImageUpdateAfterBind && Has12.descriptorBindingUpdateUnusedWhilePending
                                && Has12.shaderSampledImageArrayNonUniformIndexing
                                && xgpu::details::Env("XGPU_NO_BINDLESS") == nullptr;
#if defined(__linux__)
        // LION patch (WSL + Dozen only): Dozen's bindless DXIL path looks up
        // dx.types.ResourceProperties at bind/draw (libvulkan_dzn.so+0x136a6b
        // reads object+0x8, +0x136a6f reads that+0x30) and SIGSEGVs when either
        // pointer is null. Every crash so far (DrawPairs, setTextureTable,
        // setPipelineInstance in probe/tail/main) is that dereference of a
        // material or pipeline-instance set. Per-material sets write every
        // slot (BuildSet / pipeline_instance), so the views exist.
        // Native Linux and Windows keep the bindless heap.
        // Remove when Dozen's bindless resource annotation is null-safe.
        if( Device.m_bDozenCompat )
            Device.m_bBindless = false;
#endif

        VkPhysicalDeviceVulkan13Features Use13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
                                              , .synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE };
        VkPhysicalDeviceVulkan12Features Use12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &Use13
                                              , .drawIndirectCount                            = Has12.drawIndirectCount
                                              , .shaderSampledImageArrayNonUniformIndexing    = Device.m_bBindless
                                              , .descriptorBindingSampledImageUpdateAfterBind = Device.m_bBindless
                                              , .descriptorBindingUpdateUnusedWhilePending    = Device.m_bBindless
                                              , .descriptorBindingPartiallyBound              = Device.m_bBindless
                                              , .runtimeDescriptorArray                       = Device.m_bBindless
                                              , .hostQueryReset                               = Has12.hostQueryReset
                                              , .timelineSemaphore                            = VK_TRUE };
#if defined(__linux__)
        // LION patch (WSL + Dozen only): a 1.2 ICD does not understand Vulkan13Features;
        // enable synchronization2 and dynamic rendering through their KHR structs.
        // Native Linux and Windows use the standard Vulkan 1.3 path.
        // Remove when Dozen advertises Vulkan 1.3.
        VkPhysicalDeviceSynchronization2FeaturesKHR UseSync2{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR, .synchronization2 = VK_TRUE };
        VkPhysicalDeviceDynamicRenderingFeaturesKHR UseDynRend{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR, .pNext = &UseSync2, .dynamicRendering = VK_TRUE };
        if( Device.m_bDozenCompat )
            Use12.pNext = &UseDynRend;
#endif
        Device.m_bDrawIndirectCount = Has12.drawIndirectCount == VK_TRUE;

        // VK_EXT_memory_budget where the GPU has it: the driver's budget and usage per heap (the profiler's memory maps: device::ReportMemoryHeaps)
        std::vector<const char*>    enabledExtensions   { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        bool                        bCheckpoints        = false;
        VkPhysicalDeviceShaderClockFeaturesKHR UseClock{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR, .shaderSubgroupClock = VK_TRUE };
        VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR UseStats{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR, .pipelineExecutableInfo = VK_TRUE };
        {
            std::uint32_t n = 0;
            vkEnumerateDeviceExtensionProperties(Device.m_VKPhysicalDevice, nullptr, &n, nullptr);
            std::vector<VkExtensionProperties> Available(n);
            vkEnumerateDeviceExtensionProperties(Device.m_VKPhysicalDevice, nullptr, &n, Available.data());
            for( const auto& E : Available ) if( std::strcmp(E.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0 ) Device.m_bMemoryBudget = true;
            if( Device.m_bMemoryBudget ) enabledExtensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
#if defined(__linux__)
            // LION patch (WSL + Dozen only): enable the KHR forms of the 1.3 features we use.
            // maintenance4 is absent on Dozen and unused (vkGetBuffer/ImageMemoryRequirements).
            // VK_EXT_extended_dynamic_state is also absent: vertex streams use static strides.
            // Native Linux and Windows use the standard Vulkan 1.3 path.
            if( Device.m_bDozenCompat )
            {
                bool hasSync2Ext = false, hasDynExt = false;
                for( const auto& E : Available )
                {
                    if( std::strcmp(E.extensionName, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) == 0 ) hasSync2Ext = true;
                    if( std::strcmp(E.extensionName, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == 0 ) hasDynExt = true;
                }
                if( hasSync2Ext ) enabledExtensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
                if( hasDynExt )   enabledExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
            }
#endif
            if( xgpu::details::Env("XGPU_PIPELINE_STATS") )
                for( const auto& E : Available ) if( std::strcmp(E.extensionName, VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME) == 0 )
                {
                    Device.m_bPipelineStats = true;
                    enabledExtensions.push_back(VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME);
                    Use13.pNext = &UseStats;
#if defined(__linux__)
                    if( Device.m_bDozenCompat ) UseSync2.pNext = &UseStats;
#endif
                }
            if( xgpu::details::Env("XGPU_CHECKPOINTS") )
                for( const auto& E : Available ) if( std::strcmp(E.extensionName, VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME) == 0 ) { enabledExtensions.push_back(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME); bCheckpoints = true; }
#if defined(_WIN32)
            // (XGPU_AFTERMATH: the crash dumps' diagnostics on this device)
            static VkDeviceDiagnosticsConfigCreateInfoNV s_Diag{ .sType = VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV
                                                               , .flags = VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV | VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_AUTOMATIC_CHECKPOINTS_BIT_NV | VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_DEBUG_INFO_BIT_NV };
            if( aftermath::Enable() )
                for( const auto& E : Available ) if( std::strcmp(E.extensionName, VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME) == 0 ) { enabledExtensions.push_back(VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME); s_Diag.pNext = Use12.pNext; Use12.pNext = &s_Diag; }
#endif
            // VK_KHR_shader_clock where the GPU has it: a shader may read its subgroup's clock (xLIONRender's XLION_HEAT: a pass's cost per pixel)
            for( const auto& E : Available ) if( std::strcmp(E.extensionName, VK_KHR_SHADER_CLOCK_EXTENSION_NAME) == 0 )
            {
                enabledExtensions.push_back(VK_KHR_SHADER_CLOCK_EXTENSION_NAME);
                UseClock.pNext = Use12.pNext;
                Use12.pNext    = &UseClock;
            }
        }
        VkDeviceCreateInfo          deviceCreateInfo
        {
            .sType                      = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO
        ,   .pNext                      = &Use12
        ,   .queueCreateInfoCount       = bOwnTransferFamily ? static_cast<uint32_t>(queueCreateInfo.size()) : 1u      // one family: it may be listed only once
        ,   .pQueueCreateInfos          = queueCreateInfo.data()
        ,   .enabledLayerCount          = 0
        ,   .ppEnabledLayerNames        = nullptr
        ,   .enabledExtensionCount      = static_cast<uint32_t>(enabledExtensions.size())
        ,   .ppEnabledExtensionNames    = enabledExtensions.data()
        ,   .pEnabledFeatures           = &Features
        };

        std::vector<const char*>    ValidationLayers{};
        if( Device.m_Instance->m_bValidation )
        {
            ValidationLayers = getValidationLayers( *Device.m_Instance );
            deviceCreateInfo.enabledLayerCount          = static_cast<std::uint32_t>(ValidationLayers.size());
            deviceCreateInfo.ppEnabledLayerNames        = ValidationLayers.data();
        }

        if( auto VKErr = vkCreateDevice( Device.m_VKPhysicalDevice
                                    , &deviceCreateInfo
                                    , Device.m_Instance->m_pVKAllocator
                                    , &Device.m_VKDevice ); VKErr )
        {
            Device.m_Instance->ReportError(VKErr, "Fail to create the Vulkan Graphical Device");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create the Vulkan Graphical Device" );
        }

#if defined(__linux__)
        // LION patch (WSL + Dozen only): a 1.2 ICD does not provide the core 1.3 entry
        // points; load the KHR names the extension enabled above.
        // Native Linux and Windows use the standard Vulkan 1.3 path.
        // Remove when Dozen advertises Vulkan 1.3.
        if( Device.m_bDozenCompat )
        {
            if( auto p = reinterpret_cast<PFN_vkQueueSubmit2>(vkGetDeviceProcAddr(Device.m_VKDevice, "vkQueueSubmit2KHR")) )
                xgpu_disp_vkQueueSubmit2 = p;
            if( auto p = reinterpret_cast<PFN_vkCmdPipelineBarrier2>(vkGetDeviceProcAddr(Device.m_VKDevice, "vkCmdPipelineBarrier2KHR")) )
                xgpu_disp_vkCmdPipelineBarrier2 = p;
        }
#endif

        if( bCheckpoints )
        {
            Device.m_pCmdSetCheckpoint = reinterpret_cast<PFN_vkCmdSetCheckpointNV>(vkGetDeviceProcAddr(Device.m_VKDevice, "vkCmdSetCheckpointNV"));
            Device.m_pGetCheckpoints   = reinterpret_cast<PFN_vkGetQueueCheckpointDataNV>(vkGetDeviceProcAddr(Device.m_VKDevice, "vkGetQueueCheckpointDataNV"));
            std::printf("[xGPU] CHECKPOINTS on: %s\n", Device.m_pCmdSetCheckpoint && Device.m_pGetCheckpoints ? "every dispatch and render pass" : "the entry points are missing"); std::fflush(stdout);
        }
        return nullptr;
    }

    //----------------------------------------------------------------------------------------------------------

    // A checkpoint's marker: its text, kept for the device's life (the driver gives the pointer back)
    static std::mutex s_CheckpointLock; static std::unordered_set<std::string> s_CheckpointNames;
    void device::Checkpoint( VkCommandBuffer Cmd, const char* pKind, const char* pName ) noexcept
    {
        if( m_pCmdSetCheckpoint == nullptr ) return;
        auto& s_Lock = s_CheckpointLock; auto& s_Names = s_CheckpointNames;
        const char* p;
        { std::lock_guard Lk(s_Lock); p = s_Names.emplace(std::string(pKind) + " #" + std::to_string(m_CheckpointSeq++) + ": " + (pName ? pName : "(no name)")).first->c_str(); }
        m_pCmdSetCheckpoint(Cmd, p);
    }

    void device::PrintCheckpoints( void ) noexcept
    {
        if( m_pGetCheckpoints == nullptr ) return;
        const auto Print = [&](VkQueue Q, const char* pQueue)
        {
            if( Q == VK_NULL_HANDLE ) return;
            std::uint32_t n = 0; m_pGetCheckpoints(Q, &n, nullptr);
            std::vector<VkCheckpointDataNV> D(n, VkCheckpointDataNV{ .sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV });
            m_pGetCheckpoints(Q, &n, D.data());
            for( std::uint32_t i = 0; i < n; ++i ) std::printf("[xGPU] CHECKPOINT %s queue, stage 0x%x: %s\n", pQueue, unsigned(D[i].stage), D[i].pCheckpointMarker ? static_cast<const char*>(D[i].pCheckpointMarker) : "(none)");
            if( n == 0 ) std::printf("[xGPU] CHECKPOINT %s queue: none reported\n", pQueue);
        };
        Print(m_VKMainQueue.get(), "main");      // (first: the names below can be many - XGPU_CHECKPOINTS=2 names every draw)
        Print(m_VKAsyncQueue.get(), "async");
        std::fflush(stdout);
        if( const char* p = xgpu::details::Env("XGPU_CHECKPOINTS"); p && std::atoi(p) >= 2 ) return;
        {   // every marker seen, by its order in a frame: what each number is
            std::lock_guard Lk(s_CheckpointLock);
            std::vector<std::pair<int, std::string>> All;
            for( const auto& N : s_CheckpointNames ) { const auto h = N.find('#'); All.emplace_back(h == std::string::npos ? -1 : std::atoi(N.c_str() + h + 1), N); }
            std::sort(All.begin(), All.end());
            for( const auto& [k, N] : All ) std::printf("[xGPU] CHECKPOINT NAME %s\n", N.c_str());
        }
        Print(m_VKMainQueue.get(), "main");
        Print(m_VKAsyncQueue.get(), "async");
        std::fflush(stdout);
    }

    //----------------------------------------------------------------------------------------------------------

    xgpu::device::error* device::Initialize
    (   std::shared_ptr<xgpu::vulkan::instance>&    Instance
    ,   const xgpu::device::setup&                  Setup
    ,   std::uint32_t                               MainQueueIndex
    ,   VkPhysicalDevice                            PhysicalDevice
    ,   std::vector<VkQueueFamilyProperties>        Properties ) noexcept
    {
        // Referece back to the instance
        m_Instance          = Instance;
        m_VKPhysicalDevice  = PhysicalDevice;
        m_MainQueueIndex    = MainQueueIndex;

        //
        // Vulkan device
        //
        switch( Setup.m_Type )
        {
            case xgpu::device::type::COMPUTE:
            case xgpu::device::type::COPY:
                m_Instance->ReportError("Fail to create the Vulkan Device (This device is unsupported right now)");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create the Vulkan Device (This device is unsupported right now)");
            case xgpu::device::type::RENDER_AND_SWAP:
            case xgpu::device::type::RENDER_ONLY:
                if( auto VErr = CreateGraphicsDevice(*this, m_Instance->m_bValidation, Properties ); VErr )
                    return VErr;
            break;
        }

        //
        // Get all the queues
        //
        {
            std::scoped_lock Lks(m_VKMainQueue, m_VKTransferQueue, m_VKAsyncQueue);      // (every queue written here: Debug asserts a get() not under its lock)
            vkGetDeviceQueue(m_VKDevice, MainQueueIndex,       0, &m_VKMainQueue.get());
            if( m_bAsyncCompute ) vkGetDeviceQueue(m_VKDevice, MainQueueIndex, 1, &m_VKAsyncQueue.get());
            vkGetDeviceQueue(m_VKDevice, m_TransferQueueIndex, 0, &m_VKTransferQueue.get());
        }
        
        //
        // Gather physical device memory properties
        //
        vkGetPhysicalDeviceMemoryProperties(m_VKPhysicalDevice, &m_VKDeviceMemoryProperties);
        vkGetPhysicalDeviceProperties(m_VKPhysicalDevice, &m_VKPhysicalDeviceProperties);
        {
            VkPhysicalDeviceSubgroupProperties SubgroupProps{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES };
            VkPhysicalDeviceProperties2        Props2{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &SubgroupProps };
            vkGetPhysicalDeviceProperties2(m_VKPhysicalDevice, &Props2);
            m_bSubgroupClustered = (SubgroupProps.supportedOperations & VK_SUBGROUP_FEATURE_CLUSTERED_BIT) != 0;
        }

        // Which GPU/driver the validation messages are about (on WSLg: "Microsoft Direct3D12 (...)" = Mesa Dozen, or llvmpipe)
        if (m_Instance->m_bValidation)
            std::fprintf(stderr, "[xGPU] Vulkan device: %s (API %u.%u.%u), maxPushConstantsSize %u, %s, %s, uploads on %s\n"
                , m_VKPhysicalDeviceProperties.deviceName
                , VK_API_VERSION_MAJOR(m_VKPhysicalDeviceProperties.apiVersion), VK_API_VERSION_MINOR(m_VKPhysicalDeviceProperties.apiVersion)
                , VK_API_VERSION_PATCH(m_VKPhysicalDeviceProperties.apiVersion)
                , m_VKPhysicalDeviceProperties.limits.maxPushConstantsSize
                , m_bBindless ? "bindless" : "per-material sets"
                , m_bSubgroupClustered ? "subgroup clustered" : "no subgroup clustered"
                , m_TransferQueueIndex != m_MainQueueIndex ? "a transfer queue (ownership transfers)" : "the main queue family" );
        
        //
        // Create the Pipeline Cache
        //
        VkPipelineCacheCreateInfo   pipelineCacheCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO
        };
        if( auto VKErr = vkCreatePipelineCache( m_VKDevice
                                                , &pipelineCacheCreateInfo
                                                , m_Instance->m_pVKAllocator
                                                , &m_VKPipelineCache 
                                                ); VKErr )
        {
            m_Instance->ReportError( VKErr, "Fail to Create the pipeline cache" );
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to Create the pipeline cache");
        }

        //
        // Descriptor pool for pipeline instances: CreateSamplerDescriptorPool /
        // AllocateSamplerDescriptorSet (another pool when one is full).
        //
        {
            std::lock_guard Lk( m_LockedVKDescriptorPool );
            m_LockedVKDescriptorPool.get() = CreateSamplerDescriptorPool();
            if (m_LockedVKDescriptorPool.get() == VK_NULL_HANDLE)
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create a Descriptor Pool");
        }

        if( auto Err = InitHeap(); Err ) return Err;

        // The streaming lane: textures and buffers created with data are copied on the transfer queue
        if( auto Err = m_Upload.Init(*this); Err ) return Err;

        return nullptr;
    }

    //----------------------------------------------------------------------------------------------------------

    void device::getInstance( xgpu::instance& Instance ) const noexcept
    {
        Instance.m_Private = m_Instance;
    }

    //----------------------------------------------------------------------------------------------------------

    bool device::getMemoryType
    ( std::uint32_t     TypeBits
    , const VkFlags     Properties
    , std::uint32_t&    TypeIndex
    ) const noexcept
    {
        for( std::uint32_t i = 0; i < 32; i++ )
        {
            if( TypeBits & 1 )
            {
                if( (m_VKDeviceMemoryProperties.memoryTypes[i].propertyFlags & Properties) == Properties )
                {
                    TypeIndex = i;
                    return true;
                }
            }
            TypeBits >>= 1;
        }

        m_Instance->ReportWarning( "Fail to find memory flags" );
        return false;
    }

    //----------------------------------------------------------------------------------------------------------

    void device::FlushCommands( void )
    {

    }

    //------------------------------------------------------------------------------------------------------------------

    xgpu::device::error* device::Create
    ( xgpu::window&                     Window
    , const xgpu::window::setup&        Setup
    , std::shared_ptr<device_handle>&   SharedDevice
    ) noexcept
    {
        auto NewWindow = std::make_unique< xgpu::vulkan::window >();
        if( auto Err = NewWindow->Initialize
        ( std::reinterpret_pointer_cast<xgpu::vulkan::device>(SharedDevice)
        , Setup 
        ); Err ) return Err;

        Window.m_Private = std::move(NewWindow);

        return nullptr;
    }

    //----------------------------------------------------------------------------------------------------------

    xgpu::device::error* device::Create              
    ( xgpu::renderpass&                         Renderpass
    , const xgpu::renderpass::setup&            Setup
    , std::shared_ptr<device_handle>&           SharedDevice
    ) noexcept
    {
        auto NewRenderpass = std::make_unique< xgpu::vulkan::renderpass >();
        if (auto Err = NewRenderpass->Initialize
        ( std::reinterpret_pointer_cast<xgpu::vulkan::device>(SharedDevice)
        , Setup
        ); Err) return Err;

        Renderpass.m_Private = std::move(NewRenderpass);

        return nullptr;
    }

    //----------------------------------------------------------------------------------------------------------

    xgpu::device::error* device::Create
    ( xgpu::pipeline&                   Pipeline
    , const xgpu::pipeline::setup&      Setup
    , std::shared_ptr<device_handle>&   SharedDevice
    ) noexcept
    {
        if( auto Err = EnsureHeapDefaults(SharedDevice); Err ) return Err;
        auto Pl = std::make_shared<xgpu::vulkan::pipeline>();
        if ( auto Err = Pl->Initialize
        ( std::reinterpret_pointer_cast<xgpu::vulkan::device>(SharedDevice)
        , Setup 
        ); Err ) return Err;
        Pipeline.m_Private = std::move(Pl);
        return nullptr;
    }

    //----------------------------------------------------------------------------------------------------------
    xgpu::device::error* device::Create
    ( xgpu::buffer&                   Buffer
    , const xgpu::buffer::setup&      Setup
    , std::shared_ptr<device_handle>& SharedDevice
    ) noexcept
    {
        auto B = std::make_unique<xgpu::vulkan::buffer>();
        if( auto Err = B->Initialize
        ( std::reinterpret_pointer_cast<xgpu::vulkan::device>(SharedDevice)
        , Setup
        ); Err ) return Err;
        Buffer.m_Private = std::move(B);
        return nullptr;
    }

    //----------------------------------------------------------------------------------------------------------

    xgpu::device::error* device::Create
    ( xgpu::pipeline_instance&              PipelineInstance
    , const xgpu::pipeline_instance::setup& Setup
    , std::shared_ptr<device_handle>&       SharedDevice
    ) noexcept
    {
        if( auto Err = EnsureHeapDefaults(SharedDevice); Err ) return Err;
        auto PI = std::make_unique<xgpu::vulkan::pipeline_instance>();
        if( auto Err = PI->Initialize
        ( std::reinterpret_pointer_cast<xgpu::vulkan::device>(SharedDevice)
        , Setup 
        ); Err) return Err;
        PipelineInstance.m_Private = std::move(PI);
        return nullptr;
    }

    //----------------------------------------------------------------------------------------------------------

    xgpu::device::error* device::Create
    ( xgpu::shader&                     Shader
    , const xgpu::shader::setup&        Setup 
    , std::shared_ptr<device_handle>&   SharedDevice
    ) noexcept
    {
        auto VulkanShader = std::make_shared<xgpu::vulkan::shader>();
        if( auto Err = VulkanShader->Initialize( std::reinterpret_pointer_cast<xgpu::vulkan::device>(SharedDevice), Setup ); Err ) 
            return Err;
        Shader.m_Private = VulkanShader;
        return nullptr;
    }

    //----------------------------------------------------------------------------------------------------------

    xgpu::device::error* device::Create
    ( xgpu::vertex_descriptor&              VDescriptor
    , const xgpu::vertex_descriptor::setup& Setup
    , std::shared_ptr<device_handle>&       SharedDevice
    ) noexcept
    {
        auto VDesc = std::make_shared<xgpu::vulkan::vertex_descriptor>();
        if( auto Err = VDesc->Initialize( Setup ); Err ) 
            return Err;
        VDescriptor.m_Private = VDesc;
        return nullptr;
    }

    //----------------------------------------------------------------------------------------------------------

    xgpu::device::error* device::Create
    ( xgpu::texture&                    Texture
    , const xgpu::texture::setup&       Setup
    , std::shared_ptr<device_handle>&   SharedDevice
    ) noexcept
    {
        if( auto Err = EnsureHeapDefaults(SharedDevice); Err ) return Err;
        auto I = std::make_shared<xgpu::vulkan::texture>();
        if( auto Err = I->Initialize( std::reinterpret_pointer_cast<xgpu::vulkan::device>(SharedDevice), Setup ); Err )
            return Err;
        I->m_SamplerID = getSampler({ .m_AddressMode = Setup.m_AdressModes });
        I->m_ID        = AllocTextureID(I);
        Texture.m_Private = I;
        return nullptr;
    }

    //----------------------------------------------------------------------------------------------------------
    // The pipeline cache kept between runs. The file's header (VkPipelineCacheHeaderVersionOne) is checked here against this GPU and
    // driver before the driver sees it - drivers should reject a foreign cache themselves, not every one does it well.

    void device::LoadPipelineCache( std::wstring_view Path ) noexcept
    {
        if( xgpu::details::Env("XGPU_NO_PIPELINE_CACHE") ) return;
        std::ifstream File( std::filesystem::path(Path), std::ios::binary );
        if( !File ) return;
        const std::vector<char> Data( (std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>() );

        VkPipelineCacheHeaderVersionOne Header{};
        if( Data.size() < sizeof(Header) ) return;
        std::memcpy( &Header, Data.data(), sizeof(Header) );
        if(    Header.headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE
            || Header.vendorID      != m_VKPhysicalDeviceProperties.vendorID
            || Header.deviceID      != m_VKPhysicalDeviceProperties.deviceID
            || std::memcmp( Header.pipelineCacheUUID, m_VKPhysicalDeviceProperties.pipelineCacheUUID, VK_UUID_SIZE ) ) return;

        const VkPipelineCacheCreateInfo Info{ .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, .initialDataSize = Data.size(), .pInitialData = Data.data() };
        VkPipelineCache Loaded{};
        if( vkCreatePipelineCache( m_VKDevice, &Info, m_Instance->m_pVKAllocator, &Loaded ) ) return;
        vkMergePipelineCaches( m_VKDevice, m_VKPipelineCache, 1, &Loaded );
        vkDestroyPipelineCache( m_VKDevice, Loaded, m_Instance->m_pVKAllocator );
    }

    //----------------------------------------------------------------------------------------------------------

    xgpu::device::error* device::SavePipelineCache( std::wstring_view Path ) noexcept
    {
        std::size_t Size = 0;
        if( vkGetPipelineCacheData( m_VKDevice, m_VKPipelineCache, &Size, nullptr ) ) return VGPU_ERROR(xgpu::device::error::FAILURE, "SavePipelineCache: could not read the pipeline cache");
        if( Size == m_SavedCacheSize ) return nullptr;      // (nothing new since the last save: a caller may save every few seconds)
        std::vector<char> Data( Size );
        if( vkGetPipelineCacheData( m_VKDevice, m_VKPipelineCache, &Size, Data.data() ) ) return VGPU_ERROR(xgpu::device::error::FAILURE, "SavePipelineCache: could not read the pipeline cache");

        // written aside and then put in place: a run that dies while saving leaves the old file, not half of one
        const std::filesystem::path Final( Path ), Temp( std::wstring(Path) + L".tmp" );
        std::error_code Ec;
        std::filesystem::create_directories( Final.parent_path(), Ec );
        {
            std::ofstream File( Temp, std::ios::binary | std::ios::trunc );
            if( !File.write( Data.data(), static_cast<std::streamsize>(Size) ) ) return VGPU_ERROR(xgpu::device::error::FAILURE, "SavePipelineCache: could not write the file");
        }
        std::filesystem::rename( Temp, Final, Ec );
        if( Ec ) return VGPU_ERROR(xgpu::device::error::FAILURE, "SavePipelineCache: could not replace the file");
        m_SavedCacheSize = Size;
        return nullptr;
    }

    //----------------------------------------------------------------------------------------------------------

    // The mips above Src's first into Src's image (its other mips untouched: Src keeps being drawn meanwhile), then Out: a new view of all
    // of them with its own id. Out holds Src (m_pImageOwner), whose destruction frees the image: it goes with the last of the two.
    // ponytail: Src's own view and id live as long as Out does (one descriptor slot more per streamed texture)
    xgpu::device::error* device::ExtendTexture
    ( xgpu::texture&                                Out
    , xgpu::texture&                                Src
    , std::span<const std::span<const std::byte>>  MipFaceData
    ) noexcept
    {
        auto pSrc = std::static_pointer_cast<xgpu::vulkan::texture>(Src.m_Private);
        if (!pSrc || pSrc->m_FirstMip == 0 || !pSrc->m_VKImage) return VGPU_ERROR(xgpu::device::error::FAILURE, "ExtendTexture: the texture has no mips to add");
        const std::uint32_t First = pSrc->m_FirstMip, nFaces = pSrc->m_ArrayCount;
        if (MipFaceData.size() != std::size_t(First) * nFaces) return VGPU_ERROR(xgpu::device::error::FAILURE, "ExtendTexture: one piece per face and mip above the first is expected");

        std::vector<VkBufferImageCopy> Regions;
        std::uint64_t Offset = 0;
        for (std::uint32_t f = 0; f < nFaces; ++f)
            for (std::uint32_t m = 0; m < First; ++m)
            {
                Regions.push_back({ .bufferOffset = Offset
                                  , .imageSubresource{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = m, .baseArrayLayer = f, .layerCount = 1 }
                                  , .imageExtent{ std::max(1u, std::uint32_t(pSrc->m_Width) >> m), std::max(1u, std::uint32_t(pSrc->m_Height) >> m), 1 } });
                Offset += MipFaceData[f * First + m].size();
            }
        if (auto Err = m_Upload.UploadImage(pSrc->m_VKImage, { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = First, .layerCount = nFaces }, MipFaceData, Regions); Err) return Err;

        auto I = std::make_shared<xgpu::vulkan::texture>();
        I->m_Device      = pSrc->m_Device;
        I->m_VKImage     = pSrc->m_VKImage;
        I->m_Width       = pSrc->m_Width;
        I->m_Height      = pSrc->m_Height;
        I->m_ArrayCount  = pSrc->m_ArrayCount;
        I->m_nMips       = pSrc->m_nMips;
        I->m_Format      = pSrc->m_Format;
        I->m_VKFormat    = pSrc->m_VKFormat;
        I->m_AdressModes = pSrc->m_AdressModes;
        I->m_bCubeMap    = pSrc->m_bCubeMap;
        I->m_SamplerID   = pSrc->m_SamplerID;
        I->m_bContentReady = true;
        I->m_pImageOwner = pSrc;
        const VkImageViewCreateInfo ViewInfo{ .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = I->m_VKImage
                                            , .viewType = I->m_bCubeMap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D, .format = I->m_VKFormat
                                            , .components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A }
                                            , .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = I->m_nMips, .baseArrayLayer = 0, .layerCount = nFaces } };
        if (auto VKErr = vkCreateImageView(m_VKDevice, &ViewInfo, m_Instance->m_pVKAllocator, &I->m_VKView); VKErr)
        {
            m_Instance->ReportError(VKErr, "ExtendTexture: fail to create the image view");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "ExtendTexture: fail to create the image view");
        }
        I->m_VKDescriptorImageInfo = { .imageView = I->m_VKView, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        I->m_ID = AllocTextureID(I);
        Out.m_Private = I;
        return nullptr;
    }

    //----------------------------------------------------------------------------------------------------------

    xgpu::device::error* device::UpdateTexture
    ( xgpu::texture&                Texture
    , int                           OffsetX
    , int                           OffsetY
    , int                           Width
    , int                           Height
    , std::span<const std::byte>   Source
    ) noexcept
    {
        auto pTexture = static_cast<xgpu::vulkan::texture*>(Texture.m_Private.get());
        if (!pTexture) return VGPU_ERROR(xgpu::device::error::FAILURE, "UpdateTexture: the texture has not been created");
        return pTexture->UpdateRegion(OffsetX, OffsetY, Width, Height, Source);
    }

    //----------------------------------------------------------------------------------------------------------

    xgpu::device::error* device::ReadTexture
    ( const xgpu::texture&          Texture
    , std::vector<std::uint32_t>&  Dest
    , int&                          Width
    , int&                          Height
    ) noexcept
    {
        auto pTexture = static_cast<xgpu::vulkan::texture*>(Texture.m_Private.get());
        if (!pTexture) return VGPU_ERROR(xgpu::device::error::FAILURE, "ReadTexture: the texture has not been created");
        return pTexture->Readback(Dest, Width, Height);
    }

    //----------------------------------------------------------------------------------------------------------

    xgpu::device::error* device::ReadTexture
    ( const xgpu::texture&          Texture
    , std::vector<std::byte>&      Dest
    , int&                          Width
    , int&                          Height
    , int X, int Y, int RegionW, int RegionH
    ) noexcept
    {
        auto pTexture = static_cast<xgpu::vulkan::texture*>(Texture.m_Private.get());
        if (!pTexture) return VGPU_ERROR(xgpu::device::error::FAILURE, "ReadTexture: the texture has not been created");
        return pTexture->ReadbackRaw(Dest, Width, Height, X, Y, RegionW, RegionH);
    }

    //----------------------------------------------------------------------------------------------------------

    xgpu::device::error* device::Create
    ( xgpu::timestamps&                 Timestamps
    , const xgpu::timestamps::setup&    Setup
    , std::shared_ptr<device_handle>&   SharedDevice
    ) noexcept
    {
        auto T = std::make_unique<xgpu::vulkan::timestamps>();
        if( auto Err = T->Initialize
        ( std::reinterpret_pointer_cast<xgpu::vulkan::device>(SharedDevice)
        , Setup
        ); Err ) return Err;
        Timestamps.m_Private = std::move(T);
        return nullptr;
    }

    //----------------------------------------------------------------------------------------------------------

    xgpu::device::error* device::Create
    ( xgpu::compute&                    Compute
    , const xgpu::compute::setup&       Setup
    , std::shared_ptr<device_handle>&   SharedDevice
    ) noexcept
    {
        auto C = std::make_shared<xgpu::vulkan::compute>();
        if( auto Err = C->Initialize( std::reinterpret_pointer_cast<xgpu::vulkan::device>(SharedDevice), Setup ); Err ) return Err;
        Compute.m_Private = std::move(C);
        return nullptr;
    }

    void device::PageFlipNotification(void) noexcept
    {
        DeathMarch();
    }

    //----------------------------------------------------------------------------------------------------------
    // One frame in flight (window::EndFrame): what the CPU must not overwrite before the GPU is done with it waits here first

    void device::AddInFlight( VkFence Fence ) noexcept
    {
        std::lock_guard Lk(m_InFlightLock);
        m_InFlight.push_back(Fence);
        m_InFlightSerials.push_back(++m_SubmitSerial);
        m_InFlightFrames.push_back(m_FrameIndex);
        // (seal what was retired since the last submission: the frame just submitted, or an earlier one, is the last that can use it)
        const auto& C = m_Collecting;
        if( !(C.m_Texture.empty() && C.m_PipelineInstance.empty() && C.m_Pipeline.empty() && C.m_RenderPass.empty() && C.m_Buffer.empty() && C.m_Window.empty() && C.m_TextureTable.empty() && C.m_TableSets.empty()) )
        {
            m_Retired.push_back({ m_SubmitSerial, std::move(m_Collecting) });
            m_Collecting = {};
        }
    }

    void device::RemoveInFlight( VkFence Fence ) noexcept
    {
        std::lock_guard Lk(m_InFlightLock);
        for( std::size_t i = 0; i < m_InFlight.size(); ) if( m_InFlight[i] == Fence ) { m_InFlight.erase(m_InFlight.begin() + i); m_InFlightSerials.erase(m_InFlightSerials.begin() + i); m_InFlightFrames.erase(m_InFlightFrames.begin() + i); } else ++i;
    }

    // The frames up to F done (their fences waited): a ring with a slot a device frame writes its slot without waiting for the frames in
    // flight, only for the one that used the slot last. One window: that frame is always done already (one in flight); several windows
    // (each its own frame in flight, the device's frame moving at every window's flip): it may not be - Play's crash, 2026-11-08
    void device::WaitFramesUpTo( std::int64_t F ) noexcept
    {
        std::lock_guard Lk(m_InFlightLock);
        std::vector<VkFence> Wait;
        for( std::size_t i = 0; i < m_InFlight.size(); ++i ) if( m_InFlightFrames[i] <= F ) Wait.push_back(m_InFlight[i]);
        if( Wait.empty() ) return;
        WaitFences( static_cast<std::uint32_t>(Wait.size()), Wait.data(), "WaitFramesUpTo" );
        for( std::size_t i = 0; i < m_InFlight.size(); ) if( m_InFlightFrames[i] <= F ) { m_InFlight.erase(m_InFlight.begin() + i); m_InFlightSerials.erase(m_InFlightSerials.begin() + i); m_InFlightFrames.erase(m_InFlightFrames.begin() + i); } else ++i;
    }

    // A frame's fence, without a limit. Over 5 s something is stuck (the editor froze here with an idle GPU and no driver reset, 2026-11-05):
    // say what it could be. Uploads all done means the frame can only be waiting for its swapchain image.
    void device::WaitFences( std::uint32_t Count, const VkFence* pFences, const char* pWho ) noexcept
    {
        while( vkWaitForFences( m_VKDevice, Count, pFences, VK_TRUE, 5'000'000'000ull ) == VK_TIMEOUT )
        {
            std::uint64_t Done = 0, AcquireDone = 0;
            vkGetSemaphoreCounterValue(m_VKDevice, m_Upload.m_Timeline, &Done);
            vkGetSemaphoreCounterValue(m_VKDevice, m_Upload.m_AcquireTimeline, &AcquireDone);
            std::printf("[xGPU] FENCESTUCK %s: %u fences, first status %d; uploads %llu of %llu done, acquires %llu of %llu; device frame %llu\n"
                , pWho, Count, static_cast<int>(vkGetFenceStatus(m_VKDevice, pFences[0]))
                , static_cast<unsigned long long>(Done), static_cast<unsigned long long>(m_Upload.m_Flushed)
                , static_cast<unsigned long long>(AcquireDone), static_cast<unsigned long long>(m_Upload.m_Acquired)
                , static_cast<unsigned long long>(m_FrameIndex));
            if( m_DoneTL != VK_NULL_HANDLE )
            {
                std::uint64_t Reached = 0, Started = 0;
                vkGetSemaphoreCounterValue(m_VKDevice, m_DoneTL, &Reached);
                vkGetSemaphoreCounterValue(m_VKDevice, m_StartTL, &Started);
                std::printf("[xGPU] DONETL frames' submits: waits met %llu, finished %llu, of %llu\n", static_cast<unsigned long long>(Started), static_cast<unsigned long long>(Reached), static_cast<unsigned long long>(m_DoneSubmitted));
                for( std::uint64_t v = Reached + 1; v <= m_DoneSubmitted && v <= Reached + 4; ++v ) std::printf("[xGPU] DONETL   %s\n", m_SubmitLog[v % m_SubmitLog.size()].c_str());
                if( m_bMemoryBudget )
                {
                    VkPhysicalDeviceMemoryBudgetPropertiesEXT Budget{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT };
                    VkPhysicalDeviceMemoryProperties2         Props { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, .pNext = &Budget };
                    vkGetPhysicalDeviceMemoryProperties2(m_VKPhysicalDevice, &Props);
                    for( std::uint32_t i = 0; i < Props.memoryProperties.memoryHeapCount; ++i )
                        std::printf("[xGPU] HEAP %u%s: size %.0f MB, budget %.0f MB, used %.0f MB\n", i, (Props.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? " (device)" : "", double(Props.memoryProperties.memoryHeaps[i].size) / 1048576.0, double(Budget.heapBudget[i]) / 1048576.0, double(Budget.heapUsage[i]) / 1048576.0);
                }
                static bool s_bProgressPrinted = false;
                if( ProgressOn() && !s_bProgressPrinted ) { s_bProgressPrinted = true; ProgressPrint(Reached + 1); }
            }
            std::fflush(stdout);
            // (the NV checkpoints are valid only after a device loss: read on a timeout they proved nothing, 2026-11-11)
            static bool s_bPrinted = false; if( !s_bPrinted && xgpu::details::Env("XGPU_CHECKPOINTS_ON_STUCK") ) { s_bPrinted = true; PrintCheckpoints(); }
        }
    }

    bool device::ProgressOn( void ) noexcept { static const bool b = xgpu::details::Env("XGPU_PROGRESS") != nullptr; return b; }

    void device::ProgressBegin( VkCommandBuffer Cmd, std::uint64_t Key ) noexcept
    {
        auto& P = m_Progress[Key];
        if( P.m_Pool == VK_NULL_HANDLE )
        {
            const VkQueryPoolCreateInfo Info{ .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = progress_max_v };
            if( vkCreateQueryPool(m_VKDevice, &Info, m_Instance->m_pVKAllocator, &P.m_Pool) != VK_SUCCESS ) { P.m_Pool = VK_NULL_HANDLE; return; }
        }
        vkCmdResetQueryPool(Cmd, P.m_Pool, 0, progress_max_v);
        P.m_Names.clear();
        P.m_Q = 0;
    }

    void device::ProgressStamp( VkCommandBuffer Cmd, std::uint64_t Key, VkPipelineStageFlagBits Stage, std::string&& Name ) noexcept
    {
        auto It = m_Progress.find(Key);
        if( It == m_Progress.end() || It->second.m_Pool == VK_NULL_HANDLE || It->second.m_Q >= progress_max_v ) return;
        vkCmdWriteTimestamp(Cmd, Stage, It->second.m_Pool, It->second.m_Q++);
        It->second.m_Names.push_back(std::move(Name));
    }

    // The stuck frame (the submit of Value): each stamp written by the GPU or not, read without waiting; printed around the first missing one
    void device::ProgressPrint( std::uint64_t Value ) noexcept
    {
        const auto It = m_Progress.find(m_SubmitKey[Value % m_SubmitKey.size()]);
        if( It == m_Progress.end() || It->second.m_Q == 0 ) { std::printf("[xGPU] PROGRESS none for submit %llu\n", static_cast<unsigned long long>(Value)); return; }
        const auto& P = It->second;
        struct r { std::uint64_t m_T, m_A; };
        std::vector<r> R(P.m_Q);
        (void)vkGetQueryPoolResults(m_VKDevice, P.m_Pool, 0, P.m_Q, R.size() * sizeof(r), R.data(), sizeof(r), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        std::uint32_t nDone = 0, First = P.m_Q;
        for( std::uint32_t q = 0; q < P.m_Q; ++q ) if( R[q].m_A ) ++nDone; else if( First == P.m_Q ) First = q;
        std::printf("[xGPU] PROGRESS submit %llu: %u of %u stamps written; the first missing #%u\n", static_cast<unsigned long long>(Value), nDone, P.m_Q, First);
        const std::uint32_t From = First > 6 ? First - 6 : 0, To = std::min(P.m_Q, First + 10);
        for( std::uint32_t q = From; q < To; ++q ) std::printf("[xGPU] PROGRESS   #%u %s %s\n", q, R[q].m_A ? "done   " : "MISSING", P.m_Names[q].c_str());
        std::fflush(stdout);
    }

    void device::WaitInFlight( void ) noexcept
    {
        std::lock_guard Lk(m_InFlightLock);        // (held while waiting: a window resets a fence only after RemoveInFlight)
        if( m_InFlight.empty() ) return;
        XPROFILE_SCOPE("xGPU: CPU idle, waiting for the GPU (GPU bound)");      // (a CPU write into a buffer the GPU may read waits for the frame in flight: one frame in flight, so when the GPU is the slower side the CPU waits here - whichever zone it lands in, it is not that zone's work, Tomas read it as CPU bound 2026-11-07)
        static const bool s_bLog = xgpu::details::Env("XGPU_WAIT_LOG") != nullptr;      // (measurement: a wait that blocked, 1 ms or more - the CPU stalled on the GPU mid-frame: 2026-11-08's 14 ms in a MemoryMap)
        const auto T0 = std::chrono::steady_clock::now();
        WaitFences( static_cast<std::uint32_t>(m_InFlight.size()), m_InFlight.data(), "WaitInFlight" );
        if( s_bLog ) if( const double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count(); Ms >= 1.0 ) { std::printf("[xGPU] WAITINFLIGHT %.2f ms (frame %llu): a MemoryMap, Resize or destroy waited for the frame in flight\n", Ms, static_cast<unsigned long long>(m_FrameIndex)); std::fflush(stdout); }
        m_InFlight.clear();
        m_InFlightSerials.clear();
        m_InFlightFrames.clear();
        if( m_LastDoneFrame < m_FrameIndex - 1 ) m_LastDoneFrame = m_FrameIndex - 1;      // (every frame submitted: the one being recorded is m_FrameIndex)
    }

    //----------------------------------------------------------------------------------------------------------

    void device::Destroy(xgpu::texture&& Texture)                     noexcept 
    { 
        m_Collecting.m_Texture.push_back(std::move(Texture)); 
    }
    void device::Destroy(xgpu::pipeline_instance&& PipelineInstance)  noexcept 
    { 
        m_Collecting.m_PipelineInstance.push_back(std::move(PipelineInstance));
    }
    void device::Destroy(xgpu::pipeline&& Pipeline)                   noexcept 
    { 
        m_Collecting.m_Pipeline.push_back(std::move(Pipeline)); 
    }
    void device::Destroy(xgpu::renderpass&& RenderPass)               noexcept
    {
        m_Collecting.m_RenderPass.push_back(std::move(RenderPass));
    }
    void device::Destroy(xgpu::buffer&& Buffer)                       noexcept
    {
        m_Collecting.m_Buffer.push_back(std::move(Buffer));
    }
    void device::Destroy(xgpu::window&& Window)                       noexcept
    {
        m_Collecting.m_Window.push_back(std::move(Window));
    }

    //----------------------------------------------------------------------------------------------------------

    void device::DeathMarch(void) noexcept
    {

        m_FrameIndex++;
        static const bool s_bLeakLog = xgpu::details::Env("XGPU_LEAK_LOG") != nullptr;      // (measurement: what the device keeps, every 600 frames)
        if( s_bLeakLog && m_FrameIndex % 600 == 0 ) { extern std::atomic<std::uint64_t> g_ShaderModules, g_ShaderBytes; std::printf("XGPULEAK pipelines %zu, pipeline instances %zu, extra sampler pools %zu, shader modules made %llu (%.1f MB)\n", m_PipeLineMap.size(), m_PipeLineInstanceMap.size(), m_AdditionalSamplerPools.size(), (unsigned long long)g_ShaderModules.load(), double(g_ShaderBytes.load()) / 1048576.0); std::fflush(stdout); }

        // the batches every frame up to their serial is done with (none of those still in flight)
        std::uint64_t Oldest;
        { std::lock_guard Lk(m_InFlightLock); Oldest = m_SubmitSerial + 1; for( auto S : m_InFlightSerials ) Oldest = std::min(Oldest, S); }
        while( !m_Retired.empty() && m_Retired.front().m_Serial < Oldest )
        {
            auto B = std::move(m_Retired.front());
            m_Retired.pop_front();
            ProcessDeathMarch(B.m_Objects);
        }
    }

    void device::ProcessDeathMarch( death_march& DeathMarch ) noexcept
    {

        if (false == DeathMarch.m_PipelineInstance.empty())
        {
            for (auto& E : DeathMarch.m_PipelineInstance)
            {
                if (E.m_Private.use_count() == 1)
                {
                    {
                        std::lock_guard Lk(m_LockedVKDescriptorPool);
                        auto* pInstance = static_cast<xgpu::vulkan::pipeline_instance*>(E.m_Private.get());
                        const auto SamplerPool = pInstance->m_VKSamplerPool != VK_NULL_HANDLE
                                               ? pInstance->m_VKSamplerPool
                                               : m_LockedVKDescriptorPool.get();
                        if (auto It = m_PipeLineInstanceMap.find(pInstance->m_Serial); It != m_PipeLineInstanceMap.end())
                        {
                            // Only [0] is a sampler-pool set (window.cpp copies m_VKSamplerSet there).
                            // [1] and [2] stay VK_NULL_HANDLE; vkFreeDescriptorSets must not be passed
                            // those, or a set allocated from another pool.
                            if (It->second.m_VKDescriptorSet[0] != VK_NULL_HANDLE)
                                vkFreeDescriptorSets(m_VKDevice, SamplerPool, 1, &It->second.m_VKDescriptorSet[0]);
                            m_PipeLineInstanceMap.erase(It);
                        }
                        else if (pInstance->m_VKSamplerSet != VK_NULL_HANDLE)
                        {
                            // never drawn: no cache entry - its sampler set (allocated in Initialize) was leaking
                            vkFreeDescriptorSets(m_VKDevice, SamplerPool, 1, &pInstance->m_VKSamplerSet);
                        }
                        pInstance->m_VKSamplerSet  = VK_NULL_HANDLE;
                        pInstance->m_VKSamplerPool = VK_NULL_HANDLE;
                    }
                }
                E.m_Private.reset();
            }
            DeathMarch.m_PipelineInstance.clear();
        }

        if (false == DeathMarch.m_Pipeline.empty())
        {
            for (auto& E : DeathMarch.m_Pipeline)
            {
                if (E.m_Private.use_count() == 1)
                {
                    auto* pPipe = static_cast<xgpu::vulkan::pipeline*>(E.m_Private.get());
                    for (const auto Key : pPipe->m_CachedPipelineKeys)
                    {
                        if (auto it = m_PipeLineMap.find(Key); it != m_PipeLineMap.end())
                        {
                            if (it->second.m_nUsers > 1)
                            {
                                --it->second.m_nUsers;
                            }
                            else
                            {
                                if (it->second.m_VKPipeline)
                                    vkDestroyPipeline(m_VKDevice, it->second.m_VKPipeline, m_Instance->m_pVKAllocator);
                                m_PipeLineMap.erase(it);
                            }
                        }
                    }
                    pPipe->m_CachedPipelineKeys.clear();
                }
                E.m_Private.reset();
            }
            DeathMarch.m_Pipeline.clear();
        }

        if (false == DeathMarch.m_Buffer.empty())
        {
            // Moved out first: releasing the last reference of a buffer queues it again (~buffer -> DeathMarch -> Destroy, e.g. one
            // handed to Destroy while its owner still held a copy), and that push_back must not land in the vector being cleared
            auto Buffers = std::move(DeathMarch.m_Buffer);
            DeathMarch.m_Buffer.clear();
            Buffers.clear();
        }

        if (false == DeathMarch.m_Window.empty())
        {
            DeathMarch.m_Window.clear();
        }

        if (false == DeathMarch.m_RenderPass.empty())
        {
            for (auto& E : DeathMarch.m_RenderPass)
            {
                E.m_Private.reset();
            }
            DeathMarch.m_RenderPass.clear();
        }

        if (false == DeathMarch.m_TextureTable.empty())
        {
            // Moved out first: a table's textures, released with it, may queue themselves (m_Texture below)
            auto Tables = std::move(DeathMarch.m_TextureTable);
            DeathMarch.m_TextureTable.clear();
            for (auto& E : Tables) E.m_Private.reset();
        }

        for (auto& [Pool, Set] : DeathMarch.m_TableSets) FreeTableSet(Pool, Set);
        DeathMarch.m_TableSets.clear();

        if (false == DeathMarch.m_Texture.empty())
        {
            auto Textures = std::move(DeathMarch.m_Texture);
            DeathMarch.m_Texture.clear();
            for (auto& E : Textures)
            {
                E.m_Private.reset();
            }
        }

    }

    void device::UploadSubmitInfo( std::vector<VkCommandBufferSubmitInfo>& CmdBuffers, std::vector<VkSemaphoreSubmitInfo>& Waits, std::vector<VkSemaphoreSubmitInfo>& Signals ) noexcept
    {
        std::vector<VkCommandBuffer> Acquires;
        std::uint64_t WaitValue = 0, AcquireSignal = 0;
        m_Upload.TakeAcquires(Acquires, WaitValue, AcquireSignal);
        for( auto C : Acquires ) CmdBuffers.push_back({ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = C });
        if( WaitValue )     Waits.push_back  ({ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = m_Upload.m_Timeline, .value = WaitValue, .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT });
        if( AcquireSignal ) Signals.push_back({ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = m_Upload.m_AcquireTimeline, .value = AcquireSignal, .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT });
    }

    void device::Shutdown(void) noexcept
    {
        vkDeviceWaitIdle(m_VKDevice);

    #if XGPU_PROFILE
        if( m_pProfileContext ) { TracyVkDestroy(static_cast<tracy::VkCtx*>(m_pProfileContext)); m_pProfileContext = nullptr; }
    #endif
        // (the GPU idle: everything retired goes now)
        while( !m_Retired.empty() ) { auto B = std::move(m_Retired.front()); m_Retired.pop_front(); ProcessDeathMarch(B.m_Objects); }
        ProcessDeathMarch(m_Collecting);
    }

    //----------------------------------------------------------------------------------------------------------
    // The memory heaps as the profiler's memory spaces: VRAM (the device-local heaps), System RAM (the others) - each one's size and, with
    // VK_EXT_memory_budget, the driver's budget and usage for this process (the maps then show the rest of the card, not only xGPU's blocks)

    void device::ReportMemoryHeaps( void ) noexcept
    {
        VkPhysicalDeviceMemoryBudgetPropertiesEXT Budget{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT };
        VkPhysicalDeviceMemoryProperties2         Props { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, .pNext = m_bMemoryBudget ? &Budget : nullptr };
        vkGetPhysicalDeviceMemoryProperties2(m_VKPhysicalDevice, &Props);
        std::uint64_t Size[2]{}, Bud[2]{}, Use[2]{};
        for( std::uint32_t i = 0; i < Props.memoryProperties.memoryHeapCount; ++i )
        {
            const auto& H = Props.memoryProperties.memoryHeaps[i];
            const int   s = (H.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? 0 : 1;
            Size[s] += H.size;
            Bud[s]  += m_bMemoryBudget ? Budget.heapBudget[i] : H.size;
            Use[s]  += m_bMemoryBudget ? Budget.heapUsage[i]  : 0;
        }
        XPROFILE_MEM_SPACE("VRAM", Size[0], Bud[0], Use[0]);
        // (System RAM: xprofile asks the OS - the driver counts only GPU allocations there)
    }

    //----------------------------------------------------------------------------------------------------------
    // The profiler's Vulkan context on the main queue (xgpu_vulkan_profile.h): made on the first frame with the profiler on - Tracy
    // calibrates it with a command buffer of its own, submitted on the main queue and waited (once; the queue's lock held)
    void* device::ProfileContext(void) noexcept
    {
    #if XGPU_PROFILE
        if( m_pProfileContext || !xprofile::isEnabled() ) return m_pProfileContext;
        VkCommandPool   Pool{};
        VkCommandBuffer Cmd{};
        const VkCommandPoolCreateInfo PoolInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = m_MainQueueIndex };
        if( vkCreateCommandPool(m_VKDevice, &PoolInfo, m_Instance->m_pVKAllocator, &Pool) ) return nullptr;
        const VkCommandBufferAllocateInfo Alloc{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = Pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
        if( !vkAllocateCommandBuffers(m_VKDevice, &Alloc, &Cmd) )
        {
            std::lock_guard Lk(m_VKMainQueue);
            if( auto* p = tracy::CreateVkContext(m_VKPhysicalDevice, m_VKDevice, m_VKMainQueue.get(), Cmd, nullptr, nullptr) )     // (TracyVkContext's own macro ends in ';')
            {
                TracyVkContextName(p, "GPU: main queue", 15);
                m_pProfileContext = p;
            }
        }
        vkDestroyCommandPool(m_VKDevice, Pool, m_Instance->m_pVKAllocator);
        return m_pProfileContext;
    #else
        return nullptr;
    #endif
    }
}


