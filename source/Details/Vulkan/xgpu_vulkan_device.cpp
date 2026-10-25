namespace xgpu::vulkan
{
    //----------------------------------------------------------------------------------------------------------

    device::~device(void) noexcept
    {
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
        if( Device.m_TransferQueueIndex == 0xffffffff ) Device.m_TransferQueueIndex = Device.m_MainQueueIndex;
        const bool bOwnTransferFamily = Device.m_TransferQueueIndex != Device.m_MainQueueIndex;

        //
        // Prepare our queues
        //
        static const std::array     queuePriorities    = {    0.0f };
        auto queueCreateInfo = std::array
        {   VkDeviceQueueCreateInfo 
            {
                .sType              = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO
            ,   .queueFamilyIndex   = Device.m_MainQueueIndex
            ,   .queueCount         = static_cast<std::uint32_t>(queuePriorities.size())
            ,   .pQueuePriorities   = queuePriorities.data()
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

        // Vulkan 1.3 is the minimum (instance.cpp filters the devices): timeline semaphores and synchronization2 are core and
        // always on. Host query reset lets vulkan::timestamps recycle its queries from the CPU, so a timestamp can be written
        // anywhere in a frame, including inside a render pass (vkCmdResetQueryPool can't be).
        // Bindless (descriptor indexing): one global array of sampled images and one of samplers, indexed by texture_id /
        // sampler_id, partially bound and updated while bound. Every feature it needs, or the device uses the per-material sets
        // (XGPU_NO_BINDLESS forces those, to test them)
        VkPhysicalDeviceVulkan13Features Has13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        VkPhysicalDeviceVulkan12Features Has12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &Has13 };
        {
            VkPhysicalDeviceFeatures2 Features2{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &Has12 };
            vkGetPhysicalDeviceFeatures2( Device.m_VKPhysicalDevice, &Features2 );
        }
        if( !Has12.timelineSemaphore || !Has13.synchronization2 || !Has13.dynamicRendering )
        {
            Device.m_Instance->ReportError("xGPU needs a Vulkan 1.3 device (timeline semaphores, synchronization2, dynamic rendering)");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "xGPU needs a Vulkan 1.3 device (timeline semaphores, synchronization2, dynamic rendering)");
        }
        Device.m_bHostQueryReset = Has12.hostQueryReset == VK_TRUE;
        Device.m_bBindless       = Has12.runtimeDescriptorArray && Has12.descriptorBindingPartiallyBound
                                && Has12.descriptorBindingSampledImageUpdateAfterBind && Has12.descriptorBindingUpdateUnusedWhilePending
                                && Has12.shaderSampledImageArrayNonUniformIndexing
                                && std::getenv("XGPU_NO_BINDLESS") == nullptr;

        VkPhysicalDeviceVulkan13Features Use13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
                                              , .synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE };
        VkPhysicalDeviceVulkan12Features Use12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &Use13
                                              , .shaderSampledImageArrayNonUniformIndexing    = Device.m_bBindless
                                              , .descriptorBindingSampledImageUpdateAfterBind = Device.m_bBindless
                                              , .descriptorBindingUpdateUnusedWhilePending    = Device.m_bBindless
                                              , .descriptorBindingPartiallyBound              = Device.m_bBindless
                                              , .runtimeDescriptorArray                       = Device.m_bBindless
                                              , .hostQueryReset                               = Has12.hostQueryReset
                                              , .timelineSemaphore                            = VK_TRUE };

        static constexpr auto       enabledExtensions   = std::array
        { VK_KHR_SWAPCHAIN_EXTENSION_NAME
//        ,   VK_NV_GLSL_SHADER_EXTENSION_NAME  // nVidia useful extension to be able to load GLSL shaders
        };
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

        return nullptr;
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
            std::scoped_lock Lks(m_VKMainQueue, m_VKTransferQueue);
            vkGetDeviceQueue(m_VKDevice, MainQueueIndex,       0, &m_VKMainQueue.get());
            vkGetDeviceQueue(m_VKDevice, m_TransferQueueIndex, 0, &m_VKTransferQueue.get());
        }
        
        //
        // Gather physical device memory properties
        //
        vkGetPhysicalDeviceMemoryProperties(m_VKPhysicalDevice, &m_VKDeviceMemoryProperties);
        vkGetPhysicalDeviceProperties(m_VKPhysicalDevice, &m_VKPhysicalDeviceProperties);

        // Which GPU/driver the validation messages are about (on WSLg: "Microsoft Direct3D12 (...)" = Mesa Dozen, or llvmpipe)
        if (m_Instance->m_bValidation)
            std::fprintf(stderr, "[xGPU] Vulkan device: %s (API %u.%u.%u), maxPushConstantsSize %u, %s\n"
                , m_VKPhysicalDeviceProperties.deviceName
                , VK_API_VERSION_MAJOR(m_VKPhysicalDeviceProperties.apiVersion), VK_API_VERSION_MINOR(m_VKPhysicalDeviceProperties.apiVersion)
                , VK_API_VERSION_PATCH(m_VKPhysicalDeviceProperties.apiVersion)
                , m_VKPhysicalDeviceProperties.limits.maxPushConstantsSize
                , m_bBindless ? "bindless" : "per-material sets" );
        
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
        // Create DecriptorPool for pipelines
        // We could create different pools with different set counts to remove fragmentation from the pools
        //  So when even we need two textures we allocate from the ( 2 * max_descriptors_per_pool_v ) pool
        // When doing multi-thread this could be a contention point
        //
        {
            constexpr auto max_descriptors_per_pool_v = 1024u;
            
            const auto PoolSizes = std::array
            {   VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER,                   max_descriptors_per_pool_v }
            ,   VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,    max_descriptors_per_pool_v }
            ,   VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,             max_descriptors_per_pool_v }
            ,   VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,             max_descriptors_per_pool_v }
            ,   VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,      max_descriptors_per_pool_v }
            ,   VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,      max_descriptors_per_pool_v }
            ,   VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,            max_descriptors_per_pool_v }
            ,   VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,            max_descriptors_per_pool_v }
            ,   VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,    max_descriptors_per_pool_v }
            ,   VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,    max_descriptors_per_pool_v }
            ,   VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,          max_descriptors_per_pool_v }
            ,   VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,    max_descriptors_per_pool_v }
            };

            
//            for (auto t : PoolSizes)
//                m_DescriptorRings[t.type].Init(m_VKDevice, t.type, m_Instance->m_pVKAllocator );


            VkDescriptorPoolCreateInfo PoolCreateInfo = 
            { .sType            = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
            , .flags            = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
            , .maxSets          = static_cast<uint32_t>(1024)
            , .poolSizeCount    = static_cast<uint32_t>(PoolSizes.size())
            , .pPoolSizes       = PoolSizes.data()
            };

            {
                std::lock_guard Lk( m_LockedVKDescriptorPool );
                if (auto VKErr = vkCreateDescriptorPool(m_VKDevice, &PoolCreateInfo, m_Instance->m_pVKAllocator, &m_LockedVKDescriptorPool.get()); VKErr)
                {
                    m_Instance->ReportError(VKErr, "Fail to create a Descriptor Pool");
                    return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create a Descriptor Pool");
                }
            }
        }

        // The texture heap (bindless) and the ids' registry
        if( auto Err = InitHeap(); Err ) return Err;

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
        auto I = std::make_shared<xgpu::vulkan::texture>();
        if( auto Err = I->Initialize( std::reinterpret_pointer_cast<xgpu::vulkan::device>(SharedDevice), Setup ); Err )
            return Err;
        I->m_SamplerID = getSampler({ .m_AddressMode = Setup.m_AdressModes });
        I->m_ID        = AllocTextureID(I);
        Texture.m_Private = I;
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
    ) noexcept
    {
        auto pTexture = static_cast<xgpu::vulkan::texture*>(Texture.m_Private.get());
        if (!pTexture) return VGPU_ERROR(xgpu::device::error::FAILURE, "ReadTexture: the texture has not been created");
        return pTexture->ReadbackRaw(Dest, Width, Height);
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
    }

    void device::RemoveInFlight( VkFence Fence ) noexcept
    {
        std::lock_guard Lk(m_InFlightLock);
        std::erase(m_InFlight, Fence);
    }

    void device::WaitInFlight( void ) noexcept
    {
        std::lock_guard Lk(m_InFlightLock);        // (held while waiting: a window resets a fence only after RemoveInFlight)
        if( m_InFlight.empty() ) return;
        vkWaitForFences( m_VKDevice, static_cast<std::uint32_t>(m_InFlight.size()), m_InFlight.data(), VK_TRUE, UINT64_MAX );
        m_InFlight.clear();
        if( m_LastDoneFrame < m_FrameIndex - 1 ) m_LastDoneFrame = m_FrameIndex - 1;      // (every frame submitted: the one being recorded is m_FrameIndex)
    }

    //----------------------------------------------------------------------------------------------------------

    void device::Destroy(xgpu::texture&& Texture)                     noexcept 
    { 
        m_DeathMarchList[m_FrameIndex % m_DeathMarchList.size()].m_Texture.push_back(std::move(Texture)); 
    }
    void device::Destroy(xgpu::pipeline_instance&& PipelineInstance)  noexcept 
    { 
        m_DeathMarchList[m_FrameIndex % m_DeathMarchList.size()].m_PipelineInstance.push_back(std::move(PipelineInstance));
    }
    void device::Destroy(xgpu::pipeline&& Pipeline)                   noexcept 
    { 
        m_DeathMarchList[m_FrameIndex % m_DeathMarchList.size()].m_Pipeline.push_back(std::move(Pipeline)); 
    }
    void device::Destroy(xgpu::renderpass&& RenderPass)               noexcept
    {
        m_DeathMarchList[m_FrameIndex % m_DeathMarchList.size()].m_RenderPass.push_back(std::move(RenderPass));
    }
    void device::Destroy(xgpu::buffer&& Buffer)                       noexcept
    {
        m_DeathMarchList[m_FrameIndex % m_DeathMarchList.size()].m_Buffer.push_back(std::move(Buffer));
    }
    void device::Destroy(xgpu::window&& Window)                       noexcept
    {
        m_DeathMarchList[m_FrameIndex % m_DeathMarchList.size()].m_Window.push_back(std::move(Window));
    }

    //----------------------------------------------------------------------------------------------------------

    void device::DeathMarch(void) noexcept
    {

        m_FrameIndex++;

        auto& DeathMarch = m_DeathMarchList[m_FrameIndex % m_DeathMarchList.size()];

        if (false == DeathMarch.m_PipelineInstance.empty())
        {
            for (auto& E : DeathMarch.m_PipelineInstance)
            {
                if (E.m_Private.use_count() == 1)
                {
                    {
                        std::lock_guard Lk(m_LockedVKDescriptorPool);
                        auto* pInstance = static_cast<xgpu::vulkan::pipeline_instance*>(E.m_Private.get());
                        if (auto It = m_PipeLineInstanceMap.find(pInstance->m_Serial); It != m_PipeLineInstanceMap.end())
                        {
                            // the cached sets: [0] is the instance's sampler set (window.cpp), the others its per render pass sets
                            vkFreeDescriptorSets(m_VKDevice, m_LockedVKDescriptorPool.get(), static_cast<std::uint32_t>(pInstance->m_Pipeline->m_nVKDescriptorSetLayout), It->second.m_VKDescriptorSet.data());
                            m_PipeLineInstanceMap.erase(It);
                        }
                        else if (pInstance->m_VKSamplerSet != VK_NULL_HANDLE)
                        {
                            // never drawn: no cache entry - its sampler set (allocated in Initialize) was leaking, and the pool holds 1024 sets
                            vkFreeDescriptorSets(m_VKDevice, m_LockedVKDescriptorPool.get(), 1, &pInstance->m_VKSamplerSet);
                        }
                        pInstance->m_VKSamplerSet = VK_NULL_HANDLE;
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
                    for (auto it = m_PipeLineMap.begin(); it != m_PipeLineMap.end(); )
                    {
                        if (it->second.m_pPipeline == E.m_Private.get())
                        {
                            it = m_PipeLineMap.erase(it); // erase returns the next iterator
                        }
                        else
                        {
                            ++it;
                        }
                    }
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

    void device::Shutdown(void) noexcept
    {
        vkDeviceWaitIdle(m_VKDevice);

        DeathMarch();
    }
}


