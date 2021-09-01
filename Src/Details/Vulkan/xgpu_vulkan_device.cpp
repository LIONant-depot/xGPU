
namespace xgpu::vulkan
{
    //----------------------------------------------------------------------------------------------------------

    xgpu::device::error* CreateGraphicsDevice( device& Device, const VkDeviceQueueCreateInfo& queueCreateInfo, const bool enableValidation ) noexcept
    {
        VkPhysicalDeviceFeatures Features{};
        vkGetPhysicalDeviceFeatures( Device.m_VKPhysicalDevice, &Features );
        Features.shaderClipDistance = true;
        Features.shaderCullDistance = true;
        Features.samplerAnisotropy  = true;

        static constexpr auto       enabledExtensions   = std::array
        { 
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
//        ,   VK_NV_GLSL_SHADER_EXTENSION_NAME  // nVidia useful extension to be able to load GLSL shaders
        };
        VkDeviceCreateInfo          deviceCreateInfo
        {
            .sType                      = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO
        ,   .pNext                      = nullptr
        ,   .queueCreateInfoCount       = 1
        ,   .pQueueCreateInfos          = &queueCreateInfo
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
    ( 
        std::shared_ptr<xgpu::vulkan::instance>&    Instance
    ,   const xgpu::device::setup&                  Setup
    ,   std::uint32_t                               QueueIndex
    ,   VkPhysicalDevice                            PhysicalDevice
    ,   std::vector<VkQueueFamilyProperties>        Properties ) noexcept
    {
        // Referece back to the instance
        m_Instance          = Instance;
        m_VKPhysicalDevice  = PhysicalDevice;
        m_QueueIndex        = QueueIndex;

        //
        // Vulkan device
        //
        static const std::array     queuePriorities = { 0.0f };
        VkDeviceQueueCreateInfo     queueCreateInfo
        {
            .sType              = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO
        ,   .queueFamilyIndex   = QueueIndex
        ,   .queueCount         = static_cast<std::uint32_t>(queuePriorities.size())
        ,   .pQueuePriorities   = queuePriorities.data()
        };

        switch( Setup.m_Type )
        {
            case xgpu::device::type::COMPUTE:
            case xgpu::device::type::COPY:
                m_Instance->ReportError("Fail to create the Vulkan Device (This device is unsupported right now)");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create the Vulkan Device (This device is unsupported right now)");
            case xgpu::device::type::RENDER_AND_SWAP:
            case xgpu::device::type::RENDER_ONLY:
                if( auto VErr = CreateGraphicsDevice(*this, queueCreateInfo, m_Instance->m_bValidation ); VErr )
                    return VErr;
            break;
        }

        //
        // Get the graphics queue
        //
        vkGetDeviceQueue(m_VKDevice, QueueIndex, 0, &m_VKQueue);

        //
        // Gather physical device memory properties
        //
        vkGetPhysicalDeviceMemoryProperties(m_VKPhysicalDevice, &m_VKDeviceMemoryProperties);
        vkGetPhysicalDeviceProperties(m_VKPhysicalDevice, &m_VKPhysicalDeviceProperties);
        
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
        // We create different pools for different set counts to remove fragmentation from the pools
        // When doing multithead this could be a contention point
        // TODO: This should probably be a histogram of some kind
        //
        for( auto i=1u, end = (std::uint32_t)m_LockedVKDescriptorPools.size(); i<end; ++i )
        {
            std::uint32_t maxCountPerSet = 256;

            // We need to tell the API the number of max. requested descriptors per type
            auto typeCounts = std::array
            {
                // This example only uses one descriptor type (uniform buffer) and only
                // requests one descriptor of this type
                VkDescriptorPoolSize
                {
                    .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                ,   .descriptorCount = maxCountPerSet * i
                }
            };

            // Create the global descriptor pool for this material
            auto descriptorPoolInfo = VkDescriptorPoolCreateInfo
            {
                .sType          = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
            ,   .pNext          = nullptr
            ,   .maxSets        = maxCountPerSet
            ,   .poolSizeCount  = static_cast<std::uint32_t>(typeCounts.size())
            ,   .pPoolSizes     = typeCounts.data()
            };

            {
                std::lock_guard Lk(m_LockedVKDescriptorPools[i]);
                if (auto VKErr = vkCreateDescriptorPool(m_VKDevice, &descriptorPoolInfo, m_Instance->m_pVKAllocator, &m_LockedVKDescriptorPools[i].get()); VKErr)
                {
                    m_Instance->ReportError(VKErr, "Fail to create a Descriptor Pool");
                    return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create a Descriptor Pool");
                }
            }
        }


        return nullptr;
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
        if( auto Err = VulkanShader->Initialize
        ( std::reinterpret_pointer_cast<xgpu::vulkan::device>(SharedDevice)
        , Setup 
        ); Err ) return Err;
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
        if( auto Err = VDesc->Initialize
        ( Setup
        ); Err ) return Err;
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
        if( auto Err = I->Initialize
        ( std::reinterpret_pointer_cast<xgpu::vulkan::device>(SharedDevice)
        , Setup 
        ); Err ) return Err;
        Texture.m_Private = I;
        return nullptr;
    }
}


