namespace xgpu::vulkan
{
    //------------------------------------------------------------------------------------

    local_storage& instance::getLocalStorage( void ) noexcept
    {
        auto p = reinterpret_cast<local_storage*>(xgpu::system::instance::m_LocalStorage.getRaw());
        if( p == nullptr )
        {
            std::lock_guard<std::mutex> Guard(m_LocalStorageMutex);
            m_LocalStorageCleanup.emplace_back(std::make_unique<local_storage>());
            p = m_LocalStorageCleanup.back().get();
        }
        return *p;
    }

    //------------------------------------------------------------------------------------

    std::string_view VKErrorToString( VkResult errorCode ) noexcept
    {
        switch (errorCode)
        {
        #define STR(r) case VK_ ##r: return #r
            STR(NOT_READY);
            STR(TIMEOUT);
            STR(EVENT_SET);
            STR(EVENT_RESET);
            STR(INCOMPLETE);
            STR(ERROR_OUT_OF_HOST_MEMORY);
            STR(ERROR_OUT_OF_DEVICE_MEMORY);
            STR(ERROR_INITIALIZATION_FAILED);
            STR(ERROR_DEVICE_LOST);
            STR(ERROR_MEMORY_MAP_FAILED);
            STR(ERROR_LAYER_NOT_PRESENT);
            STR(ERROR_EXTENSION_NOT_PRESENT);
            STR(ERROR_FEATURE_NOT_PRESENT);
            STR(ERROR_INCOMPATIBLE_DRIVER);
            STR(ERROR_TOO_MANY_OBJECTS);
            STR(ERROR_FORMAT_NOT_SUPPORTED);
            STR(ERROR_SURFACE_LOST_KHR);
            STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
            STR(SUBOPTIMAL_KHR);
            STR(ERROR_OUT_OF_DATE_KHR);
            STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
            STR(ERROR_VALIDATION_FAILED_EXT);
            STR(ERROR_INVALID_SHADER_NV);
        #undef STR
        default:
            return "UNKNOWN_ERROR";
        }
    }

    //------------------------------------------------------------------------------------------------------------------
    void instance::ReportError( std::string_view Message, const std::source_location& Location ) noexcept
    {
        if (m_pErrorCallback == nullptr) return;
        m_pErrorCallback
        ( xgpu::FormatString("%s(%d/%d) [%s] ERROR: %s"
        , Location.file_name()
        , Location.line()
        , Location.column()
        , Location.function_name()
        , Message.data() 
        ));
    }

    //------------------------------------------------------------------------------------------------------------------
    void instance::ReportError ( VkResult ErrorCode, std::string_view Message, const std::source_location& Location ) noexcept
    {
        if (m_pErrorCallback==nullptr) return;
        m_pErrorCallback
        ( xgpu::FormatString( "%s(%d/%d) [%s] ERROR VK%d (%s): %s"
        , Location.file_name()
        , Location.line()
        , Location.column()
        , Location.function_name()
        , ErrorCode
        , VKErrorToString(ErrorCode).data()
        , Message.data() 
        ));
    }

    //------------------------------------------------------------------------------------------------------------------
    void instance::ReportWarning( std::string_view Message, const std::source_location& Location ) noexcept
    {
        if (m_pWarningCallback == nullptr) return;
        m_pWarningCallback
        ( xgpu::FormatString("%s(%d/%d) [%s] WARNING: %s" 
        , Location.file_name()
        , Location.line()
        , Location.column()
        , Location.function_name()
        , Message.data()
        ));
    }

    //------------------------------------------------------------------------------------------------------------------
    void instance::ReportWarning( VkResult ErrorCode, std::string_view Message, const std::source_location& Location ) noexcept
    {
        if (m_pWarningCallback ==nullptr) return;
        m_pWarningCallback
        ( xgpu::FormatString( "%s(%d/%d) [%s] WARNING VK%d (%s): %s"
        , Location.file_name()
        , Location.line()
        , Location.column()
        , Location.function_name()
        , ErrorCode
        , VKErrorToString(ErrorCode).data(), Message.data() 
        ));
    }

    //------------------------------------------------------------------------------------------------------------------

    VkResult FilterValidationLayers( xgpu::vulkan::instance& Instance, std::vector<const char*>& Layers, std::span<const char* const> FilterView ) noexcept
    {
        std::uint32_t    ValidationLayerCount = -1;
        if( auto Err = vkEnumerateInstanceLayerProperties( &ValidationLayerCount, nullptr ); Err ) 
        {
            Instance.ReportError( Err, "FilterValidationLayers::vkEnumerateInstanceLayerProperties" );
            return Err;
        }
    
        auto LayerProperties = std::make_unique<VkLayerProperties[]>(ValidationLayerCount);
        if( auto Err = vkEnumerateInstanceLayerProperties( &ValidationLayerCount, LayerProperties.get() ); Err )
        {
            Instance.ReportError( Err, "FilterValidationLayers::vkEnumerateInstanceLayerProperties" );
            return Err;
        }

        //
        // Find all possible filters from our FilterView
        //    
        Layers.clear();
        for ( const auto& LayerEntry : std::span<VkLayerProperties>{ LayerProperties.get(), ValidationLayerCount } )
            for( const auto& pFilterEntry : FilterView )
                if( strcmp( pFilterEntry, LayerEntry.layerName ) == 0 )
                {
                    Layers.push_back(pFilterEntry);
                    break;
                }

        return VK_SUCCESS;
    }

    //------------------------------------------------------------------------------------------------------------------

    std::vector<const char*> getValidationLayers( vulkan::instance& Instance )
    {
        constexpr static auto s_ValidationLayerNames_Alt1 = std::array
        {
            "VK_LAYER_KHRONOS_validation"
        };

        constexpr static auto s_ValidationLayerNames_Alt2 = std::array
        {
            "VK_LAYER_GOOGLE_threading",     "VK_LAYER_LUNARG_parameter_validation",
            "VK_LAYER_LUNARG_device_limits", "VK_LAYER_LUNARG_object_tracker",
            "VK_LAYER_LUNARG_image",         "VK_LAYER_LUNARG_core_validation",
            "VK_LAYER_LUNARG_swapchain",     "VK_LAYER_GOOGLE_unique_objects",
        };

        // Try alt list first 
        std::vector<const char*> Layers;

        FilterValidationLayers( Instance, Layers, s_ValidationLayerNames_Alt1 );
        if (Layers.size() != s_ValidationLayerNames_Alt1.size())
        {
            Instance.ReportWarning("Fail to get the standard validation layers");

            // if we are not successfull then try our best with the second version
            FilterValidationLayers(Instance, Layers, s_ValidationLayerNames_Alt2);
            if (Layers.size() != s_ValidationLayerNames_Alt2.size())
            {
                Instance.ReportWarning("Fail to get all the basoc validation layers that we wanted");
            }
        }

        return std::move(Layers);
    }

    //------------------------------------------------------------------------------------
    static
    VkBool32 DebugMessageCallback(
        VkDebugReportFlagsEXT       flags,
        VkDebugReportObjectTypeEXT  objType,
        uint64_t                    srcObject,
        size_t                      location,
        int32_t                     msgCode,
        const char*                 pLayerPrefix,
        const char*                 pMsg,
        void*                       pUserData ) noexcept
    {
        auto& Instance = *reinterpret_cast<instance*>(pUserData);

        if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
        {
            Instance.ReportError( (VkResult)msgCode, xgpu::FormatString( "[%s] Code %d : %s", pLayerPrefix, msgCode, pMsg ) );
        }
        else if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
        {
            Instance.ReportWarning( (VkResult)msgCode, xgpu::FormatString("[%s] Code %d : %s", pLayerPrefix, msgCode, pMsg) );
        }
        else if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT )
        {
            Instance.ReportWarning((VkResult)msgCode, xgpu::FormatString("[%s] Code %d : %s", pLayerPrefix, msgCode, pMsg));
        }

        return false;
    }

    //------------------------------------------------------------------------------------------------------------------

    xgpu::instance::error* instance::CreateVKInstance( const xgpu::instance::setup& Setup ) noexcept
    {
        //
        // initialize the VkApplicationInfo structure
        //
        VkApplicationInfo AppInfo
        {
            .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO
        ,   .pNext              = nullptr
        ,   .pApplicationName   = Setup.m_pAppName ? Setup.m_pAppName : "xGPU::Vulkan"
        ,   .applicationVersion = 1
        ,   .pEngineName        = "xGPU::Vulkan"
        ,   .engineVersion      = 1
        ,   .apiVersion         = VK_API_VERSION_1_2
        };

        //
        // Determine which extensions to enable
        //
        std::vector<const char*>    Extensions 
        { VK_KHR_SURFACE_EXTENSION_NAME 
        // , VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME      // (NOT WORKING ON MANY GRAPHICS CARDS WILL HAVE TO WAIT...) Allows to change the vertex input to a pipeline (which should have been the default behavior)
        };

        {
            // Enable surface extensions depending on os
            #if defined(_WIN32)
                Extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
            #elif defined(VK_USE_PLATFORM_ANDROID_KHR)
                Extensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
            #elif defined(_DIRECT2DISPLAY)
                Extensions.push_back(VK_KHR_DISPLAY_EXTENSION_NAME);
            #elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
                Extensions.push_back(VK_EXT_DIRECTFB_SURFACE_EXTENSION_NAME);
            #elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
                Extensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
            #elif defined(VK_USE_PLATFORM_XCB_KHR)
                Extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
            #elif defined(VK_USE_PLATFORM_IOS_MVK)
                Extensions.push_back(VK_MVK_IOS_SURFACE_EXTENSION_NAME);
            #elif defined(VK_USE_PLATFORM_MACOS_MVK)
                Extensions.push_back(VK_MVK_MACOS_SURFACE_EXTENSION_NAME);
            #endif
        }

        //
        // Check if we need to set any validation layers
        //
        std::vector<const char*> Layers;
        if (Setup.m_bDebugMode)
        {
            Layers = getValidationLayers( *this );
            if( Layers.size() ) 
            {
                Extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                Extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
            }
        }

        //
        // Enable render doc if requested by the user
        //
        if( Setup.m_bEnableRenderDoc )
        {
            Layers.emplace_back( "VK_LAYER_RENDERDOC_Capture" );
        }

        //
        // initialize the VkInstanceCreateInfo structure
        //
        VkInstanceCreateInfo InstanceInfo
        {
            .sType                      = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
        ,   .pNext                      = nullptr
        ,   .flags                      = 0
        ,   .pApplicationInfo           = &AppInfo
        ,   .enabledLayerCount          = static_cast<std::uint32_t>(Layers.size())
        ,   .ppEnabledLayerNames        = Layers.size() ? Layers.data() : nullptr
        ,   .enabledExtensionCount      = static_cast<std::uint32_t>(Extensions.size())
        ,   .ppEnabledExtensionNames    = Extensions.size() ? Extensions.data() : nullptr
        };

        //
        // Ready to create the instance now
        //
        if (VkResult Res = vkCreateInstance( &InstanceInfo, nullptr, &m_VKInstance ); Res)
        {
            ReportError(Res, "Fail to create the Vulkan Instance" );
            return VGPU_ERROR( xgpu::instance::error::FAILURE, "Fail to create the Vulkan Instance" );
        }

        //
        // Setup the debug callbacks
        //
        if ( Setup.m_bDebugMode && (Setup.m_pLogErrorFunc || Setup.m_pLogWarning) )
        {
            // VkDebugReportCallbackEXT dbgBreakCallback            = (PFN_vkDebugReportMessageEXT)vkGetInstanceProcAddr        ( instance, "vkDebugReportMessageEXT"           );

            VkDebugReportCallbackCreateInfoEXT dbgCreateInfo
            {
                .sType                          = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT
            ,   .pNext                          = nullptr
            ,   .flags                          = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT
            ,   .pfnCallback                    = (PFN_vkDebugReportCallbackEXT)DebugMessageCallback
            ,   .pUserData                      = this
            };

            PFN_vkCreateDebugReportCallbackEXT  CreateDebugReportCallback = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(m_VKInstance, "vkCreateDebugReportCallbackEXT");
            VkDebugReportCallbackEXT            debugReportCallback;

            if( CreateDebugReportCallback )
            {
                if( auto VKErr = CreateDebugReportCallback( m_VKInstance, &dbgCreateInfo, nullptr, &debugReportCallback ); VKErr )
                {
                    ReportError( VKErr, "Failed to create the callback that collects all the warnings & errors from vulkan" );
                }
            }
        }

        return nullptr;
    }

    //------------------------------------------------------------------------------------------------------------------

    instance::~instance(void) noexcept
    {
        if(m_VKInstance) vkDestroyInstance(m_VKInstance, nullptr);
        m_VKInstance = nullptr;
    }

    //------------------------------------------------------------------------------------------------------------------

    xgpu::device::error* instance::CollectPhysicalDevices( std::vector<VkPhysicalDevice>& PhysicalDevices, xgpu::device::discreate DecreateType ) noexcept
    {
        //
        // Now collect all the physical devices
        //
        PhysicalDevices.resize(100);
        {
            auto MaxDevices = static_cast<std::uint32_t>(PhysicalDevices.size());
            if (auto VKErr = vkEnumeratePhysicalDevices(m_VKInstance, &MaxDevices, PhysicalDevices.data()); VKErr || MaxDevices == 0)
            {
                ReportError(VKErr, "Vulkan Could not enumerate phyiscal devices");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Vulkan Could not enumerate phyiscal devices" );
            }

            // Match the collected size
            PhysicalDevices.resize(MaxDevices);
        }

        //
        // Filter our devices that don't support the minimum version required for our implementation
        //
        for (std::size_t i = 0; i < PhysicalDevices.size(); ++i)
        {
            VkPhysicalDeviceProperties DeviceProperties;
            vkGetPhysicalDeviceProperties(PhysicalDevices[i], &DeviceProperties);
            if( DeviceProperties.apiVersion < VK_API_VERSION_1_2 )
            {
                PhysicalDevices.erase(PhysicalDevices.begin() + i);
                i--;
            }
        }

        //
        // Filter out base of the expected Discreateness
        //
        if (DecreateType == xgpu::device::discreate::NON_DISCREATE_ONLY)
        {
            for (std::size_t i=0; i< PhysicalDevices.size(); ++i )
            {
                VkPhysicalDeviceProperties DeviceProperties;
                vkGetPhysicalDeviceProperties( PhysicalDevices[i], &DeviceProperties );
                if( DeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU )
                {
                    PhysicalDevices.erase( PhysicalDevices.begin() + i );
                    i--;
                }
            }
        }
        else if (DecreateType == xgpu::device::discreate::DISCREATE_ONLY)
        {
            for (std::size_t i = 0; i < PhysicalDevices.size(); ++i)
            {
                VkPhysicalDeviceProperties DeviceProperties;
                vkGetPhysicalDeviceProperties(PhysicalDevices[i], &DeviceProperties);
                if( DeviceProperties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU )
                {
                    PhysicalDevices.erase(PhysicalDevices.begin() + i);
                    i--;
                }
            }
        }
        else if( PhysicalDevices.size() )
        {
            // Prioritize devices that are discreate to do that
            // we need to collect all sort the discreate devices first
            auto DeviceProperties = std::make_unique<VkPhysicalDeviceProperties[]>(PhysicalDevices.size());
            auto IndexArray       = std::make_unique<int[]>(PhysicalDevices.size());
            for (std::size_t i = 0; i < PhysicalDevices.size(); ++i)
            {
                vkGetPhysicalDeviceProperties( PhysicalDevices[i], &DeviceProperties[i]);
                IndexArray[i] = static_cast<int>(i);
            }

            // Sort all the devices such the descrite devices are first (We always prioritize those)
            std::sort(&IndexArray[0], &IndexArray[PhysicalDevices.size()-1], [&](const int& iA, const int& iB) -> bool
            {
                const int   a = DeviceProperties[iA].deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
                const int   b = DeviceProperties[iB].deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
                return a < b;
            });
        }

        return nullptr;
    }

    //------------------------------------------------------------------------------------------------------------------

    xgpu::device::error* instance::CreateDevice( xgpu::device& Device, const xgpu::device::setup& Setup ) noexcept
    {
        static constexpr auto QueueType = []() constexpr
        {
            std::array<VkQueueFlagBits, static_cast<std::size_t>(xgpu::device::type::ENUM_COUNT)> QueueType{};

            QueueType[static_cast<std::size_t>(xgpu::device::type::RENDER_ONLY)]    = VK_QUEUE_GRAPHICS_BIT;
            QueueType[static_cast<std::size_t>(xgpu::device::type::RENDER_AND_SWAP)]= VK_QUEUE_GRAPHICS_BIT;
            QueueType[static_cast<std::size_t>(xgpu::device::type::COMPUTE)]        = VK_QUEUE_COMPUTE_BIT;
            QueueType[static_cast<std::size_t>(xgpu::device::type::COPY)]           = VK_QUEUE_TRANSFER_BIT;

            return QueueType;
        }();

        //
        // Now collect all the physical devices
        //
        std::vector<VkPhysicalDevice> PhysicalDevices;
        if( auto Err = CollectPhysicalDevices( PhysicalDevices, Setup.m_Discreate ); Err )
            return Err;

        //
        // Collect the properties for each device
        // and choose the right device
        //
        for( const auto& PhysicalDevice : PhysicalDevices )
        {
            std::uint32_t                           MaxProperties = 100;
            std::vector<VkQueueFamilyProperties>    DeviceProps( MaxProperties );

            vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &MaxProperties, DeviceProps.data());
            if( MaxProperties <= 0 )
            {
                ReportError( "Could not get device properties" );
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Could not get device properties" );
            }

            // Match the collected properties
            DeviceProps.resize(MaxProperties);

            //
            // Check if it is a desirable device
            //
            for( auto& Prop : DeviceProps )
            {
                // TODO: Make sure it chooses the smaller subset queue
                // If property is the same type as the one requested by the user then we should consider it
                if( Prop.queueFlags & QueueType[static_cast<std::size_t>(Setup.m_Type)] )
                {
                    //
                    // Try to officially create the device
                    //
                    auto VulkanDevice = std::make_shared<xgpu::vulkan::device>();
                    auto QueueIndex   = static_cast<std::uint32_t>(static_cast<std::size_t>( &Prop - DeviceProps.data()));
                    if( auto Err = VulkanDevice->Initialize( m_Self, Setup, QueueIndex, PhysicalDevice, std::move(DeviceProps) ); Err )
                        return Err;

                    // Officially set it as the new device
                    Device.m_Private = VulkanDevice;
                    break;
                }
            }
        }

        if( Device.m_Private.get() == nullptr )
        {
            ReportError("Fail to find a compatible device from the one requested");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to find a compatible device from the one requested" );
        }

        return nullptr;
    }

    //------------------------------------------------------------------------------------------------------------------

    bool instance::ProcessInputEvents( void ) noexcept
    {
        return xgpu::system::instance::ProcessInputEvents();
    }

    //------------------------------------------------------------------------------------------------------------------

    xgpu::keyboard::error* instance::CreateKeyboard(xgpu::keyboard& Keyboard, const xgpu::keyboard::setup& Setup) noexcept
    {
        Keyboard.m_Private = xgpu::system::instance::m_Keyboard;
        return nullptr;
    }

    //------------------------------------------------------------------------------------------------------------------

    xgpu::mouse::error* instance::CreateMouse(xgpu::mouse& Mouse, const xgpu::mouse::setup& Setup) noexcept
    {
        Mouse.m_Private = xgpu::system::instance::m_Mouse;
        return nullptr;
    }
}

//------------------------------------------------------------------------------------------------------------------

xgpu::instance::error* CreateInstanceVulkan( xgpu::instance& Instance, const xgpu::instance::setup& Setup ) noexcept
{
    auto VGPUInst = std::make_shared<xgpu::vulkan::instance>();

    // Setup the error handling function
    VGPUInst->m_pErrorCallback      = Setup.m_pLogErrorFunc;
    VGPUInst->m_pWarningCallback    = Setup.m_pLogWarning;
    VGPUInst->m_bValidation         = Setup.m_bDebugMode;
    VGPUInst->m_Self                = VGPUInst;
    VGPUInst->m_AppName             = Setup.m_pAppName;

    // Officially Create the Vulkan Instance
    if(auto Err = VGPUInst->CreateVKInstance(Setup); Err ) return Err;

    // Everything finish correctly
    Instance.m_Private = std::move(VGPUInst); //std::static_pointer_cast<xgpu::details::instance_handle>( std::make_shared<xgpu::vulkan_instance>() );

    return nullptr;
}
