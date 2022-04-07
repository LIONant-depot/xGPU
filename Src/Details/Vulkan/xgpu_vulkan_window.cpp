namespace xgpu::vulkan
{
    // Helper structure to hold the data needed by one rendering frame
    struct frame
    {
        VkCommandPool       m_VKCommandPool     {};
        VkCommandBuffer     m_VKCommandBuffer   {};
        VkFence             m_VKFence           {};
        VkImage             m_VKBackbuffer      {};
        VkImageView         m_VKBackbufferView  {};
        VkFramebuffer       m_VKFramebuffer     {};
    };

    struct frame_semaphores
    {
        VkSemaphore         m_VKImageAcquiredSemaphore  {};
        VkSemaphore         m_VKRenderCompleteSemaphore {};
    };

    //------------------------------------------------------------------------------------------------------------------------

    VkSurfaceFormatKHR SelectSurfaceFormat
    (   VkPhysicalDevice            VKDevice
    ,   VkSurfaceKHR                VKSurface
    ,   std::span<const VkFormat>   RequestFormats
    ,   VkColorSpaceKHR             RequestColorSpace 
    ) noexcept
    {
        // Per Spec Format and View Format are expected to be the same unless VK_IMAGE_CREATE_MUTABLE_BIT was set at image creation
        // Assuming that the default behavior is without setting this bit, there is no need for separate Swapchain image and image view format
        // Additionally several new color spaces were introduced with Vulkan Spec v1.0.40,
        // hence we must make sure that a format with the mostly available color space, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, is found and used.

        std::vector<VkSurfaceFormatKHR> SurfaceFormats(100);
        {
            auto Count = static_cast<std::uint32_t>(SurfaceFormats.size());
            if (auto VKErr = vkGetPhysicalDeviceSurfaceFormatsKHR(VKDevice, VKSurface, &Count, SurfaceFormats.data()); VKErr)
                return {};
            SurfaceFormats.resize(Count);
        }

        // First check if only one format, VK_FORMAT_UNDEFINED, is available, which would imply that any format is available
        if( SurfaceFormats.size() == 1 )
        {
            if ( SurfaceFormats[0].format == VK_FORMAT_UNDEFINED )
            {
                VkSurfaceFormatKHR Ret;
                Ret.format     = RequestFormats[0];
                Ret.colorSpace = RequestColorSpace;
                return Ret;
            }
        }
        else
        {
            // Request several formats, the first found will be used
            for ( const auto& R : RequestFormats )
                for ( const auto& S : SurfaceFormats )
                    if ( R == S.format && S.colorSpace == RequestColorSpace)
                        return S;
        }

        // No point in searching another format
        return SurfaceFormats[0];
    }

    //------------------------------------------------------------------------------------------------------------------------

    VkFormat SelectDepthFormat
    (   VkPhysicalDevice            VKDevice
    ,   std::span<const VkFormat>   RequestFormats
    ,   VkImageTiling               Tiling
    ,   VkFormatFeatureFlags        Features
    )
    {
        for( VkFormat Format : RequestFormats ) 
        {
            VkFormatProperties Props;
            vkGetPhysicalDeviceFormatProperties(VKDevice, Format, &Props );
            if( Tiling == VK_IMAGE_TILING_LINEAR && (Props.linearTilingFeatures & Features) == Features ) 
            {
                return Format;
            }
            else if ( Tiling == VK_IMAGE_TILING_OPTIMAL && (Props.optimalTilingFeatures & Features) == Features ) 
            {
                return Format;
            }
        }

        //Unalbe locate a good z buffer format
        assert(false);
        return RequestFormats[0];
    }

    //------------------------------------------------------------------------------------------------------------------------

    VkPresentModeKHR SelectPresentMode
    (   VkPhysicalDevice                    VKDevice
    ,   VkSurfaceKHR                        VKSurface
    ,   std::span<const VkPresentModeKHR>   request_modes )
    {
        // Request a certain mode and confirm that it is available. If not use VK_PRESENT_MODE_FIFO_KHR which is mandatory
        std::vector<VkPresentModeKHR> PresentModes(100);
        {
            std::uint32_t Count = 100;
            if( auto VKErr = vkGetPhysicalDeviceSurfacePresentModesKHR(VKDevice, VKSurface, &Count, PresentModes.data() ); VKErr )
                return VK_PRESENT_MODE_FIFO_KHR;
            PresentModes.resize(Count);
        }

        for ( const auto& R : request_modes )
            for (const auto& P : PresentModes )
                if ( R == P )
                    return P;

        return VK_PRESENT_MODE_FIFO_KHR; // Always available
    }

    //------------------------------------------------------------------------------------------------------------------------

    void MinimalDestroyFrame( VkDevice VKDevice, frame& Frame, const VkAllocationCallbacks* pAllocator ) noexcept
    {
        vkDestroyFence          ( VKDevice, Frame.m_VKFence,          pAllocator);
        vkFreeCommandBuffers    ( VKDevice, Frame.m_VKCommandPool, 1, &Frame.m_VKCommandBuffer );
        vkDestroyCommandPool    ( VKDevice, Frame.m_VKCommandPool,    pAllocator);

        Frame.m_VKFence           = VK_NULL_HANDLE;
        Frame.m_VKCommandBuffer   = VK_NULL_HANDLE;
        Frame.m_VKCommandPool     = VK_NULL_HANDLE;

        vkDestroyImageView      (VKDevice, Frame.m_VKBackbufferView, pAllocator );
        vkDestroyFramebuffer    (VKDevice, Frame.m_VKFramebuffer,    pAllocator );
    }

    //------------------------------------------------------------------------------------------------------------------------

    void MinimalDestroyFrameSemaphores( VkDevice VKDevice, frame_semaphores& FrameSemaphores, const VkAllocationCallbacks* pAllocator ) noexcept
    {
        vkDestroySemaphore( VKDevice, FrameSemaphores.m_VKImageAcquiredSemaphore,  pAllocator );
        vkDestroySemaphore( VKDevice, FrameSemaphores.m_VKRenderCompleteSemaphore, pAllocator );

        FrameSemaphores.m_VKImageAcquiredSemaphore  = VK_NULL_HANDLE;
        FrameSemaphores.m_VKRenderCompleteSemaphore = VK_NULL_HANDLE;
    }

    //------------------------------------------------------------------------------------------------------------------------

    xgpu::device::error* window::CreateDepthResources( VkExtent2D Extents ) noexcept
    {
        VkImageCreateInfo ImageInfo
        { .sType            = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO
        , .flags            = 0
        , .imageType        = VK_IMAGE_TYPE_2D
        , .format           = m_VKDepthFormat
        , .extent           =
            { .width        = Extents.width
            , .height       = Extents.height
            , .depth        = 1
            }
        , .mipLevels        = 1
        , .arrayLayers      = 1
        , .samples          = VK_SAMPLE_COUNT_1_BIT
        , .tiling           = VK_IMAGE_TILING_OPTIMAL
        , .usage            = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
        , .sharingMode      = VK_SHARING_MODE_EXCLUSIVE
        , .initialLayout    = VK_IMAGE_LAYOUT_UNDEFINED
        };

        if( auto VKErr = vkCreateImage(m_Device->m_VKDevice, &ImageInfo, m_Device->m_Instance->m_pVKAllocator, &m_VKDepthbuffer ); VKErr )
        {
            m_Device->m_Instance->ReportError(VKErr, "Fail to create the depth buffer image");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create the depth buffer image");
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements( m_Device->m_VKDevice, m_VKDepthbuffer, &memRequirements );

        std::uint32_t MemoryIndex;
        if( false == m_Device->getMemoryType( memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, MemoryIndex ) )
        {
            m_Device->m_Instance->ReportError("Fail to find the right type of memory to allocate the zbuffer");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to find the right type of memory to allocate the zbuffer");
        }
        VkMemoryAllocateInfo AllocInfo
        { .sType            = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
        , .allocationSize   = memRequirements.size
        , .memoryTypeIndex  = MemoryIndex
        };

        if( auto VKErr = vkAllocateMemory( m_Device->m_VKDevice, &AllocInfo, m_Device->m_Instance->m_pVKAllocator, &m_VKDepthbufferMemory ); VKErr ) 
        {
            m_Device->m_Instance->ReportError(VKErr, "Fail to allocate memory for the zbuffer");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to allocate memory for the zbuffer");
        }

        if( auto VKErr = vkBindImageMemory(m_Device->m_VKDevice, m_VKDepthbuffer, m_VKDepthbufferMemory, 0); VKErr )
        {
            m_Device->m_Instance->ReportError(VKErr, "Fail to bind the zbuffer with its image/memory");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to bind the zbuffer with its image/memory");
        }

        VkImageViewCreateInfo ViewInfo
        { .sType                        = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
        , .image                        = m_VKDepthbuffer
        , .viewType                     = VK_IMAGE_VIEW_TYPE_2D
        , .format                       = m_VKDepthFormat
        , .subresourceRange             =
            { .aspectMask               = VK_IMAGE_ASPECT_DEPTH_BIT
            , .baseMipLevel             = 0
            , .levelCount               = 1
            , .baseArrayLayer           = 0
            , .layerCount               = 1
            }
        };

        if( auto VKErr = vkCreateImageView( m_Device->m_VKDevice, &ViewInfo, m_Device->m_Instance->m_pVKAllocator, &m_VKDepthbufferView); VKErr ) 
        {
            m_Device->m_Instance->ReportError(VKErr, "Fail to create the depth buffer view");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create the depth buffer view");
        }

        return nullptr;
    }

    //------------------------------------------------------------------------------------------------------------------------
    // Also destroy old swap chain and in-flight frames data, if any.
    xgpu::device::error* window::CreateWindowSwapChain( int Width, int Height ) noexcept
    {
        const auto VKOldSwapchain = m_VKSwapchain;

        m_VKSwapchain = NULL;

        VkAllocationCallbacks* pAllocator = m_Device->m_Instance->m_pVKAllocator;

        if( auto VKErr = vkDeviceWaitIdle(m_Device->m_VKDevice); VKErr )
        {
            m_Device->m_Instance->ReportError( VKErr, "Fail to wait for device" );
            // We will pretend that there was actually no error since this function is used for run time...
        }

        // We don't use DestroyWindow() because
        // we want to preserve the old swapchain to create the new one.
        // Destroy old Framebuffer
        if( m_Frames.get() )
        {
            for( std::uint32_t i = 0; i < m_ImageCount; i++ )
            {
                MinimalDestroyFrame          ( m_Device->m_VKDevice, m_Frames[i],           pAllocator );
                MinimalDestroyFrameSemaphores( m_Device->m_VKDevice, m_FrameSemaphores[i],  pAllocator );
            }
            m_Frames.reset();
            m_FrameSemaphores.reset();

            //
            // Release the depth buffer as well
            //
            vkDestroyImageView(m_Device->m_VKDevice, m_VKDepthbufferView, m_Device->m_Instance->m_pVKAllocator );
            vkDestroyImage( m_Device->m_VKDevice, m_VKDepthbuffer, m_Device->m_Instance->m_pVKAllocator );
            vkFreeMemory( m_Device->m_VKDevice, m_VKDepthbufferMemory, m_Device->m_Instance->m_pVKAllocator );
        }

        //
        // Destroy render pass and pipeline
        //
        if ( auto Err = DestroyRenderPass(); Err ) return Err;

        if ( m_VKPipeline )
            vkDestroyPipeline( m_Device->m_VKDevice, m_VKPipeline, pAllocator );

        //
        // Create Swapchain
        //
        {
            VkSwapchainCreateInfoKHR SwapChainInfo
            {
                .sType              = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR
            ,   .pNext              = nullptr
            ,   .flags              = 0
            ,   .surface            = m_VKSurface
            ,   .minImageCount      = m_ImageCount
            ,   .imageFormat        = m_VKSurfaceFormat.format
            ,   .imageColorSpace    = m_VKSurfaceFormat.colorSpace
            ,   .imageExtent
                {
                    .width          = static_cast<std::uint32_t>(xgpu::system::window::m_Width)
                ,   .height         = static_cast<std::uint32_t>(xgpu::system::window::m_Height)
                }
            ,   .imageArrayLayers   = 1
            ,   .imageUsage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
            ,   .imageSharingMode   = VK_SHARING_MODE_EXCLUSIVE             // Assume that graphics family == present family
            ,   .preTransform       = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
            ,   .compositeAlpha     = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
            ,   .presentMode        = m_VKPresentMode
            ,   .clipped            = VK_TRUE
            ,   .oldSwapchain       = VKOldSwapchain
            };

            VkSurfaceCapabilitiesKHR SurfaceCapabilities{};
            if( auto VKErr = vkGetPhysicalDeviceSurfaceCapabilitiesKHR( m_Device->m_VKPhysicalDevice, m_VKSurface, &SurfaceCapabilities); VKErr )
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to get the physical device surface capabilities");
                // We will pretend that there was actually no error since this function is used for run time...
            }
            else
            {
                if( m_ImageCount == 0 || m_ImageCount < SurfaceCapabilities.minImageCount )
                {
                    m_ImageCount = SurfaceCapabilities.minImageCount;
                }
                else if ( m_ImageCount > SurfaceCapabilities.maxImageCount )
                {
                    m_ImageCount = SurfaceCapabilities.maxImageCount;
                    m_Device->m_Instance->ReportWarning( "Reducing the down how many buffers we can utilize to render as device surface does not support as many as requested");
                }

                if( SurfaceCapabilities.currentExtent.width != 0xffffffff )
                {
                    SwapChainInfo.imageExtent.width  = xgpu::system::window::m_Width  = SurfaceCapabilities.currentExtent.width;
                    SwapChainInfo.imageExtent.height = xgpu::system::window::m_Height = SurfaceCapabilities.currentExtent.height;
                }
            }

            if( auto Err = CreateDepthResources(SwapChainInfo.imageExtent); Err )
                return Err;

            if( auto VKErr = vkCreateSwapchainKHR( m_Device->m_VKDevice, &SwapChainInfo, pAllocator, &m_VKSwapchain ); VKErr )
            {
                m_Device->m_Instance->ReportError( VKErr, "Fail to create the Swap Chain" );
                return VGPU_ERROR( xgpu::device::error::FAILURE, "Fail to create the Swap Chain" );
            }

            if( auto VKErr = vkGetSwapchainImagesKHR( m_Device->m_VKDevice, m_VKSwapchain, &m_ImageCount, NULL ); VKErr )
            {
                m_Device->m_Instance->ReportError( VKErr, "Unable to get the Swap Chain Images" );
                return VGPU_ERROR( xgpu::device::error::FAILURE, "Unable to get the Swap Chain Images" );
            }

            auto BackBuffers = std::make_unique<VkImage[]>(m_ImageCount);
            if( auto VKErr = vkGetSwapchainImagesKHR( m_Device->m_VKDevice, m_VKSwapchain, &m_ImageCount, BackBuffers.get() ); VKErr )
            {
                m_Device->m_Instance->ReportError(VKErr, "Unable to get the Swap Chain Images V:2");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Unable to get the Swap Chain Images V:2");
            }

            assert( m_Frames.get() == nullptr );
            m_Frames            = std::make_unique<frame[]>(m_ImageCount);
            m_FrameSemaphores   = std::make_unique<frame_semaphores[]>(m_ImageCount);

            for( std::uint32_t i = 0; i < m_ImageCount; i++ )
                m_Frames[i].m_VKBackbuffer = BackBuffers[i];
        }

        //
        // Finally destroy the old swap cahin
        //
        if ( VKOldSwapchain )
            vkDestroySwapchainKHR( m_Device->m_VKDevice, VKOldSwapchain, pAllocator );

        //
        // Create the Render Pass
        //
        if( auto Err = CreateRenderPass(m_VKSurfaceFormat, m_VKDepthFormat ); Err )
            return Err;

        //
        // Create The Image Views
        //
        {
            VkImageViewCreateInfo CreateInfo
            {
                .sType              = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
            ,   .viewType           = VK_IMAGE_VIEW_TYPE_2D
            ,   .format             = m_VKSurfaceFormat.format
            ,   .components
                {
                    .r              = VK_COMPONENT_SWIZZLE_R
                ,   .g              = VK_COMPONENT_SWIZZLE_G
                ,   .b              = VK_COMPONENT_SWIZZLE_B
                ,   .a              = VK_COMPONENT_SWIZZLE_A
                }
            ,   .subresourceRange
                {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT
                ,   .baseMipLevel   = 0
                ,   .levelCount     = 1
                ,   .baseArrayLayer = 0
                ,   .layerCount     = 1
                }
            };

            for( std::uint32_t i = 0; i < m_ImageCount; i++ )
            {
                auto& Frame = m_Frames[i];
                CreateInfo.image = Frame.m_VKBackbuffer;

                if( auto VKErr = vkCreateImageView( m_Device->m_VKDevice, &CreateInfo, pAllocator, &Frame.m_VKBackbufferView ); VKErr )
                {
                    m_Device->m_Instance->ReportError(VKErr, "Unable to create an Image View for one of the back buffers");
                    return VGPU_ERROR(xgpu::device::error::FAILURE, "Unable to create an Image View for one of the back buffers");
                }
            }
        }

        //
        // Create Framebuffer
        //
        {
            std::array<VkImageView,2> Attachment;
            VkFramebufferCreateInfo CreateInfo
            {
                .sType              = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
            ,   .renderPass         = m_VKRenderPass
            ,   .attachmentCount    = static_cast<std::uint32_t>(Attachment.size())
            ,   .pAttachments       = Attachment.data()
            ,   .width              = static_cast<std::uint32_t>(xgpu::system::window::m_Width)
            ,   .height             = static_cast<std::uint32_t>(xgpu::system::window::m_Height)
            ,   .layers             = 1
            };

            for( std::uint32_t i = 0; i < m_ImageCount; i++ )
            {
                auto& Frame = m_Frames[i];
                Attachment[0] = Frame.m_VKBackbufferView;
                Attachment[1] = m_VKDepthbufferView;
                if( auto VKErr = vkCreateFramebuffer( m_Device->m_VKDevice, &CreateInfo, pAllocator, &Frame.m_VKFramebuffer ); VKErr )
                {
                    m_Device->m_Instance->ReportError(VKErr, "Unable to create a Frame Buffer");
                    return VGPU_ERROR(xgpu::device::error::FAILURE, "Unable to create a Frame Buffer");
                }
            }
        }

        return nullptr;
    }

    //------------------------------------------------------------------------------------------------------------------------

    xgpu::device::error* window::CreateWindowCommandBuffers( void ) noexcept
    {
        VkAllocationCallbacks* pAllocator = m_Device->m_Instance->m_pVKAllocator;

        for( std::uint32_t i = 0; i < m_ImageCount; i++ )
        {
            auto& Frame             = m_Frames[i];

            {
                VkCommandPoolCreateInfo CreateInfo
                {
                    .sType              = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO
                ,   .flags              = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
                ,   .queueFamilyIndex   = m_Device->m_MainQueueIndex
                };
                if( auto VKErr = vkCreateCommandPool( m_Device->m_VKDevice, &CreateInfo, pAllocator, &Frame.m_VKCommandPool ); VKErr )
                {
                    m_Device->m_Instance->ReportError(VKErr, "Unable to create a Frame's Command Pool");
                    return VGPU_ERROR(xgpu::device::error::FAILURE, "Unable to create a Frame's Command Pool");
                }
            }

            {
                VkCommandBufferAllocateInfo CreateInfo
                {
                    .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
                ,   .commandPool        = Frame.m_VKCommandPool
                ,   .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY
                ,   .commandBufferCount = 1
                };
                if( auto VKErr = vkAllocateCommandBuffers( m_Device->m_VKDevice, &CreateInfo, &Frame.m_VKCommandBuffer ); VKErr )
                {
                    m_Device->m_Instance->ReportError(VKErr, "Unable to create a Frame's Command Buffer");
                    return VGPU_ERROR(xgpu::device::error::FAILURE, "Unable to create a Frame's Command Buffer");
                }
            }

            {
                VkFenceCreateInfo CreateInfo
                {
                    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
                ,   .flags = VK_FENCE_CREATE_SIGNALED_BIT
                };
                if( auto VKErr = vkCreateFence( m_Device->m_VKDevice, &CreateInfo, pAllocator, &Frame.m_VKFence ); VKErr )
                {
                    m_Device->m_Instance->ReportError(VKErr, "Unable to create a Frame's Fense");
                    return VGPU_ERROR(xgpu::device::error::FAILURE, "Unable to create a Frame's Fense");
                }
            }

            {
                auto&                   FrameSemaphores = m_FrameSemaphores[i];
                VkSemaphoreCreateInfo   CreateInfo
                {
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
                };

                if( auto VKErr = vkCreateSemaphore( m_Device->m_VKDevice, &CreateInfo, pAllocator, &FrameSemaphores.m_VKImageAcquiredSemaphore ); VKErr )
                {
                    m_Device->m_Instance->ReportError(VKErr, "Unable to create a Frame's Image Semaphore");
                    return VGPU_ERROR(xgpu::device::error::FAILURE, "Unable to create a Frame's Image Semaphore");
                }

                if( auto VKErr = vkCreateSemaphore( m_Device->m_VKDevice, &CreateInfo, pAllocator, &FrameSemaphores.m_VKRenderCompleteSemaphore ); VKErr )
                {
                    m_Device->m_Instance->ReportError(VKErr, "Unable to create a Frame's Render Semaphore");
                    return VGPU_ERROR(xgpu::device::error::FAILURE, "Unable to create a Frame's Render Semaphore");
                }
            }
        }

        return nullptr;
    }

    //------------------------------------------------------------------------------------------------------------------------

    xgpu::device::error* window::CreateOrResizeWindow( int Width, int Height ) noexcept
    {
        if( auto Err = CreateWindowSwapChain( Width, Height ); Err )
            return Err;

        if( auto Err = CreateWindowCommandBuffers(); Err )
            return Err;

        return nullptr;
    }

    //--------------------------------------------------------------------------------------------

    xgpu::device::error* window::CreateRenderPass( VkSurfaceFormatKHR& VKColorSurfaceFormat, VkFormat& VKDepthSurfaceFormat ) noexcept
    {
        //
        // Create the Render Pass
        //
        auto ColorAttachmentRef = std::array
        {
            VkAttachmentReference
            {
                .attachment = 0
            ,   .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            }
        };
        auto DepthAttachmentRef = std::array
        {
            VkAttachmentReference
            {
                .attachment = 1
            ,   .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            }
        };
        auto SubpassDesc = std::array
        {
            VkSubpassDescription 
            {
                .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS
            ,   .colorAttachmentCount    = static_cast<std::uint32_t>(ColorAttachmentRef.size())
            ,   .pColorAttachments       = ColorAttachmentRef.data()
            ,   .pDepthStencilAttachment = DepthAttachmentRef.data()
            }
        };
        auto SubpassDependency = std::array
        {
            VkSubpassDependency 
            {
                .srcSubpass         = VK_SUBPASS_EXTERNAL
            ,   .dstSubpass         = 0
            ,   .srcStageMask       = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
            ,   .dstStageMask       = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
            ,   .srcAccessMask      = 0
            ,   .dstAccessMask      = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
            }
        };
        auto AttachmentDesc = std::array
        {
            //
            // Color Attachment
            //
            VkAttachmentDescription
            {
                .format             = VKColorSurfaceFormat.format
            ,   .samples            = VK_SAMPLE_COUNT_1_BIT
            ,   .loadOp             = m_bClearOnRender ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE
            ,   .storeOp            = VK_ATTACHMENT_STORE_OP_STORE
            ,   .stencilLoadOp      = VK_ATTACHMENT_LOAD_OP_DONT_CARE
            ,   .stencilStoreOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE
            ,   .initialLayout      = VK_IMAGE_LAYOUT_UNDEFINED
            ,   .finalLayout        = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
            }
            //
            // Depth Attachment
            //
        ,   VkAttachmentDescription
            {
                .format             = VKDepthSurfaceFormat
            ,   .samples            = VK_SAMPLE_COUNT_1_BIT
            ,   .loadOp             = VK_ATTACHMENT_LOAD_OP_CLEAR
            ,   .storeOp            = VK_ATTACHMENT_STORE_OP_DONT_CARE
            ,   .stencilLoadOp      = VK_ATTACHMENT_LOAD_OP_DONT_CARE
            ,   .stencilStoreOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE
            ,   .initialLayout      = VK_IMAGE_LAYOUT_UNDEFINED
            ,   .finalLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            }
        };
        auto CreateInfo = VkRenderPassCreateInfo
        {
            .sType              = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO
        ,   .attachmentCount    = static_cast<std::uint32_t>(AttachmentDesc.size())
        ,   .pAttachments       = AttachmentDesc.data()
        ,   .subpassCount       = static_cast<std::uint32_t>(SubpassDesc.size())
        ,   .pSubpasses         = SubpassDesc.data()
        ,   .dependencyCount    = static_cast<std::uint32_t>(SubpassDependency.size())
        ,   .pDependencies      = SubpassDependency.data()
        };

        if( auto VKErr = vkCreateRenderPass( m_Device->m_VKDevice, &CreateInfo, m_Device->m_Instance->m_pVKAllocator, &m_VKRenderPass ); VKErr )
        {
            m_Device->m_Instance->ReportError(VKErr, "Unable to create a render pass for the window");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Unable to create a render pass for the window");
        }

        return nullptr;
    }

    //------------------------------------------------------------------------------------------------------------------------

    xgpu::device::error* window::DestroyRenderPass( void ) noexcept
    {
        if (m_VKRenderPass)
            vkDestroyRenderPass(m_Device->m_VKDevice, m_VKRenderPass, m_Device->m_Instance->m_pVKAllocator);

        m_VKRenderPass = VK_NULL_HANDLE;
        return nullptr;
    }

    //------------------------------------------------------------------------------------------------------------------------

    xgpu::device window::getDevice( void ) const noexcept
    {
        return xgpu::device{ m_Device };
    }

    //------------------------------------------------------------------------------------------------------------------------

    xgpu::device::error* window::Initialize
    ( std::shared_ptr<vulkan::device>&& Device
    , const xgpu::window::setup&        Setup 
    ) noexcept
    {
        m_Device            = Device;
        m_bClearOnRender    = Setup.m_bClearOnRender;
        m_VKClearValue[0]   = VkClearValue{ .color = {.float32 = {Setup.m_ClearColorR,Setup.m_ClearColorG,Setup.m_ClearColorB,Setup.m_ClearColorA}} };

        auto& Instance = m_Device->m_Instance;



        // TODO: Implement the user definable page count... or delete this if double buffer is best
        // If min image count was not specified, request different count of images dependent on selected present mode
//        if (min_image_count == 0)
//            min_image_count = ImGui_ImplVulkanH_GetMinImageCountFromPresentMode(wd->PresentMode);



        //
        // Create the OS window
        //
        if( auto Err = xgpu::system::window::Initialize( Setup, *Instance ); Err )
            return Err;

        //
        // Create the surface
        //
        if constexpr( std::is_same_v<xgpu::system::window, xgpu::windows::window> )
        {
            // Get the Surface creation extension since we are about to use it
            auto VKCreateWin32Surface = (PFN_vkCreateWin32SurfaceKHR)vkGetInstanceProcAddr(Instance->m_VKInstance, "vkCreateWin32SurfaceKHR");
            if( nullptr == VKCreateWin32Surface )
            {
                Instance->ReportError("Vulkan Driver missing the VK_KHR_win32_surface extension" );
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Vulkan Driver missing the VK_KHR_win32_surface extension" );
            }

            VkWin32SurfaceCreateInfoKHR SurfaceCreateInfo
            {
                .sType      = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR
            ,   .pNext      = nullptr
            ,   .flags      = 0
            ,   .hinstance  = GetModuleHandle(NULL)
            ,   .hwnd       = reinterpret_cast<HWND>(xgpu::system::window::getSystemWindowHandle())
            };

            if( auto VKErr = VKCreateWin32Surface(Instance->m_VKInstance, &SurfaceCreateInfo, nullptr, &m_VKSurface ); VKErr )
            {
                Instance->ReportError(VKErr, "Vulkan Fail to create window surface");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Vulkan Fail to create window surface");
            }
        }

        //
        // Check to see if the selected queue supports presentation to a given surface
        // (We are restricting to must be able to present. However this could be possible
        //  to solve by adding another present queue)
        {
            VkBool32 res = VK_FALSE;
            auto VKErr = vkGetPhysicalDeviceSurfaceSupportKHR(m_Device->m_VKPhysicalDevice, m_Device->m_MainQueueIndex, m_VKSurface, &res);
            if ( VKErr || res != VK_TRUE )
            {
                if(VKErr) Instance->ReportError(VKErr, "Error no WSI support on physical device");
                else      Instance->ReportError("Error no WSI support on physical device");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Error no WSI support on physical device");
            }
        }

        //
        // Select a surface format
        //
        constexpr auto requestSurfaceImageFormat = std::array
        {
            VK_FORMAT_B8G8R8A8_UNORM
        ,   VK_FORMAT_R8G8B8A8_UNORM
        ,   VK_FORMAT_B8G8R8_UNORM
        ,   VK_FORMAT_R8G8B8_UNORM
        };

        constexpr auto requestSurfaceColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

        m_VKSurfaceFormat = SelectSurfaceFormat(  m_Device->m_VKPhysicalDevice
                                                , m_VKSurface
                                                , requestSurfaceImageFormat
                                                , requestSurfaceColorSpace );

        //
        // Select Depth Format
        //
        constexpr auto requestDepthFormats = std::array
        { VK_FORMAT_D32_SFLOAT
        , VK_FORMAT_D32_SFLOAT_S8_UINT
        , VK_FORMAT_D24_UNORM_S8_UINT
        };

        m_VKDepthFormat = SelectDepthFormat( m_Device->m_VKPhysicalDevice
                                           , requestDepthFormats
                                           , VK_IMAGE_TILING_OPTIMAL
                                           , VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
                                           );
        //
        // Select Present Mode
        // FIXME-VULKAN: Even thought mailbox seems to get us maximum framerate with a single window,
        //               it halves framerate with a second window etc. (w/ Nvidia and SDK 1.82.1)
        // The following list is in priority order
        constexpr auto PresentModes = std::array
        {
            VK_PRESENT_MODE_MAILBOX_KHR
        ,   VK_PRESENT_MODE_IMMEDIATE_KHR
        ,   VK_PRESENT_MODE_FIFO_KHR
        };
        m_VKPresentMode = SelectPresentMode( m_Device->m_VKPhysicalDevice
                                           , m_VKSurface
                                           , PresentModes );

        if( false == Setup.m_bSyncOn )
        {
            m_VKPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
        else
        {
            if( m_VKPresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR )
                m_VKPresentMode = VK_PRESENT_MODE_FIFO_KHR;
        }

        // Create SwapChain, RenderPass, Framebuffer, etc.
        if( auto Err = CreateOrResizeWindow( xgpu::system::window::m_Width, xgpu::system::window::m_Height ); Err )
            return Err;


        // TODO: Not sure what to do with this...
        // data->WindowOwned = true;


        return nullptr;
    }

    //------------------------------------------------------------------------------------------------------------------------

    void window::PageFlip( void ) noexcept
    {
        (void)EndFrame();

        auto& Frame       = m_Frames[m_FrameIndex];
        auto& Semaphore   = m_FrameSemaphores[m_SemaphoreIndex];

        auto                PresetIndex = m_FrameIndex;
        VkPresentInfoKHR    Info        = 
        { .sType                = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR
        , .waitSemaphoreCount   = 1
        , .pWaitSemaphores      = &Semaphore.m_VKRenderCompleteSemaphore
        , .swapchainCount       = 1
        , .pSwapchains          = &m_VKSwapchain
        , .pImageIndices        = &PresetIndex
        };

        std::lock_guard Lk(m_Device->m_VKMainQueue);
        if( auto VKErr = vkQueuePresentKHR( m_Device->m_VKMainQueue.get(), &Info); VKErr )
        {
            if( VKErr == VK_ERROR_OUT_OF_DATE_KHR || VKErr == VK_SUBOPTIMAL_KHR )
            {
                if (auto Err = CreateOrResizeWindow(xgpu::system::window::m_Width, xgpu::system::window::m_Height); Err)
                {
                    // TODO: Report Error???
                    assert(false);
                }
            }
            else
            {
                assert(false);
            }
        }

        m_FrameIndex     = (m_FrameIndex + 1)     % m_ImageCount;
        m_SemaphoreIndex = (m_SemaphoreIndex + 1) % m_ImageCount;
    }

    //------------------------------------------------------------------------------------------------------------------------

    void window::CmdRenderBegin(xgpu::cmd_buffer& xGPUCmdBuffer) noexcept
    {
        assert(m_BeginState>0);

        if( m_BeginState == 1 )
        {
            auto& Frame = m_Frames[m_FrameIndex];

            //
            // Setup the renderpass
            //
            VkRenderPassBeginInfo RenderPassBeginInfo =
            { .sType            = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
            , .renderPass       = m_VKRenderPass
            , .framebuffer      = Frame.m_VKFramebuffer
            , .renderArea       = {.extent = { .width = static_cast<std::uint32_t>(getWidth())        // TODO: This should be the swapChainExtent size
                                             , .height = static_cast<std::uint32_t>(getHeight())
                                             }
                                  }
            , .clearValueCount  = m_bClearOnRender ? static_cast<std::uint32_t>(m_VKClearValue.size()) : 0u
            , .pClearValues     = m_bClearOnRender ? m_VKClearValue.data() : nullptr
            };
            vkCmdBeginRenderPass(Frame.m_VKCommandBuffer, &RenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            //
            // Set the default viewport
            //
            m_DefaultViewport = VkViewport
            { .x        = 0
            , .y        = 0
            , .width    = (float)getWidth()
            , .height   = (float)getHeight()
            , .minDepth = 0.0f
            , .maxDepth = 1.0f
            };

            auto Viewport = std::array{ m_DefaultViewport };

            vkCmdSetViewport
            ( Frame.m_VKCommandBuffer
            , 0
            , static_cast<std::uint32_t>(Viewport.size())
            , Viewport.data()
            );

            //
            // Set the default Scissor
            //
            m_DefaultScissor.offset.x       = 0;
            m_DefaultScissor.offset.y       = 0;
            m_DefaultScissor.extent.width   = getWidth();
            m_DefaultScissor.extent.height  = getHeight();

            auto Scissor = std::array{ m_DefaultScissor };

            vkCmdSetScissor
            ( Frame.m_VKCommandBuffer
            , 0
            , static_cast<std::uint32_t>(Scissor.size())
            , Scissor.data()
            );

            // We add an additional one here since we should not repeat this code again for this frame
            m_BeginState++;
        }

        // Ready
        m_BeginState++;

        //
        // Return the command buffer
        //
        xGPUCmdBuffer.m_pWindow = static_cast<xgpu::details::window_handle*>(this);
        auto&            CB     = reinterpret_cast<cmdbuffer&>(xGPUCmdBuffer.m_Memory);
        
        CB.m_pActiveRenderPass  = nullptr;
        CB.m_VKActiveRenderPass = m_VKRenderPass;
        CB.m_VKCommandBuffer    = m_Frames[m_FrameIndex].m_VKCommandBuffer;
        CB.m_VKPipelineLayout   = nullptr;
    }

    //------------------------------------------------------------------------------------------------------------------------

    void window::CmdRenderBegin( xgpu::cmd_buffer& xGPUCmdBuffer, const xgpu::renderpass& XGPURenderpass) noexcept
    {
        assert(m_BeginState == 1);

        auto& RenderPass = *std::reinterpret_pointer_cast<xgpu::vulkan::renderpass>(XGPURenderpass.m_Private).get();
        auto& Frame      = m_Frames[m_FrameIndex];

        vkCmdBeginRenderPass(Frame.m_VKCommandBuffer, &RenderPass.m_VKRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        const auto width    = (float)RenderPass.m_TextureAttachments[0]->m_Width;
        const auto height   = (float)RenderPass.m_TextureAttachments[0]->m_Height;

        auto Viewport = std::array
        {
            VkViewport
            { .x        = 0
            , .y        = 0
            , .width    = width
            , .height   = height
            , .minDepth = 0.0f
            , .maxDepth = 1.0f
            }
        };

        vkCmdSetViewport
        ( Frame.m_VKCommandBuffer
        , 0
        , static_cast<std::uint32_t>(Viewport.size())
        , Viewport.data()
        );

        auto Scissor = std::array
        { VkRect2D{
            .offset = VkOffset2D
            { .x = 0
            , .y = 0
            }
        ,   .extent = VkExtent2D
            { .width  = static_cast<std::uint32_t>(Viewport[0].width)
            , .height = static_cast<std::uint32_t>(Viewport[0].height)
            }
        }};

        vkCmdSetScissor
        ( Frame.m_VKCommandBuffer
        , 0
        , static_cast<std::uint32_t>(Scissor.size())
        , Scissor.data()
        );

        // Ready
        m_BeginState++;

        //
        // Return the command buffer
        //
        xGPUCmdBuffer.m_pWindow = static_cast<xgpu::details::window_handle*>(this);
        auto&            CB     = reinterpret_cast<cmdbuffer&>(xGPUCmdBuffer.m_Memory);
        
        CB.m_pActiveRenderPass  = &RenderPass;
        CB.m_VKActiveRenderPass = RenderPass.m_VKRenderPass;
        CB.m_VKCommandBuffer    = Frame.m_VKCommandBuffer;
        CB.m_VKPipelineLayout   = nullptr;
    }

    //------------------------------------------------------------------------------------------------------------------------

    bool window::BegingRendering( void ) noexcept
    {
        assert(m_BeginState == 0);
        if(xgpu::system::window::BegingRendering() )
            return true;

        // Mark as we have started the frame
        m_BeginState++;

        //
        // Check if window has change its size
        //
        if (xgpu::system::window::getResizedAndReset())
        {
            vkDeviceWaitIdle(m_Device->m_VKDevice);
            (void)CreateOrResizeWindow(getWidth(), getHeight());
        }

        //
        // Make sure we are sync with the previous frame
        //
        {
            auto& Frame     = m_Frames[m_FrameIndex];
            auto& Semaphore = m_FrameSemaphores[m_SemaphoreIndex];

            //
            // Make sure that the previous frame has finish rendering
            // Loop idle while we wait...
            for (;;)
            {
                if( auto VKErr = vkWaitForFences( m_Device->m_VKDevice, 1, &Frame.m_VKFence, VK_TRUE, 100 ); VKErr )
                {
                    if ( VKErr == VK_TIMEOUT ) continue;

                    // TODO: Report Error???
                    m_Device->m_Instance->ReportError(VKErr, "vkWaitForFences");
                    assert(false);
                }
                else
                {
                    break;
                }
            }

            if( auto VKErr = vkAcquireNextImageKHR( m_Device->m_VKDevice, m_VKSwapchain, UINT64_MAX, Semaphore.m_VKImageAcquiredSemaphore, VK_NULL_HANDLE, &m_FrameIndex ); VKErr )
            {
                //if( VKErr == VK_ERROR_OUT_OF_DATE_KHR )
                // TODO: Report Error???
                m_Device->m_Instance->ReportError(VKErr, "vkAcquireNextImageKHR");
                assert(false);
            }
        }

        //
        // Reset the command buffer
        //
        {
            auto& Frame = m_Frames[m_FrameIndex];

            if( auto VKErr = vkResetCommandPool(m_Device->m_VKDevice, Frame.m_VKCommandPool, 0); VKErr )
            {
                // TODO: Report Error???
                m_Device->m_Instance->ReportError(VKErr, "vkResetCommandPool");
                assert(false);
            }

            VkCommandBufferBeginInfo CommandBufferBeginInfo = 
            { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
            , .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
            };

            if( auto VKErr = vkBeginCommandBuffer( Frame.m_VKCommandBuffer, &CommandBufferBeginInfo ); VKErr )
            {
                m_Device->m_Instance->ReportError(VKErr, "vkBeginCommandBuffer");
                assert(false);
            }
        }
        
        return false;
    }

    //------------------------------------------------------------------------------------------------------------------------

    void window::CmdRenderEnd( xgpu::cmd_buffer& CmdBuffer ) noexcept
    {
        m_BeginState--;
        if( m_BeginState == 1 )
        {
            auto& CB = reinterpret_cast<cmdbuffer&>(CmdBuffer.m_Memory);

            // Officially end the pass
            vkCmdEndRenderPass(CB.m_VKCommandBuffer);
        }
        assert(m_BeginState>0);
    }

    //------------------------------------------------------------------------------------------------------------------------

    xgpu::device::error* window::EndFrame(void) noexcept
    {
        auto& Frame     = m_Frames[m_FrameIndex];
        auto& Semaphore = m_FrameSemaphores[m_SemaphoreIndex];
        
        // End the render pass 
        if(m_BeginState == 2)
        {
            vkCmdEndRenderPass(Frame.m_VKCommandBuffer);
        }
        else
        {
            assert(m_BeginState == 1);
        }
        m_BeginState = 0;

        // Officially end the Commands
        if( auto VKErr = vkEndCommandBuffer( Frame.m_VKCommandBuffer ); VKErr )
        {
            m_Device->m_Instance->ReportError(VKErr, "vkEndCommandBuffer");
            assert(false);
        }

        // Reset the frame fense to know when we finish with the frame
        if( auto VKErr = vkResetFences( m_Device->m_VKDevice, 1, &Frame.m_VKFence ); VKErr )
        {
            m_Device->m_Instance->ReportError(VKErr, "vkResetFences");
            assert(false);
        }

        //
        // Submit the frame into the queue for processing
        //
        VkPipelineStageFlags WaitStage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo         SubmitInfo = 
        { .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO
        , .waitSemaphoreCount   = 1
        , .pWaitSemaphores      = &Semaphore.m_VKImageAcquiredSemaphore
        , .pWaitDstStageMask    = &WaitStage
        , .commandBufferCount   = 1
        , .pCommandBuffers      = &Frame.m_VKCommandBuffer
        , .signalSemaphoreCount = 1
        , .pSignalSemaphores    = &Semaphore.m_VKRenderCompleteSemaphore
        };

        std::lock_guard Lk(m_Device->m_VKMainQueue);
        if( auto VKErr = vkQueueSubmit( m_Device->m_VKMainQueue.get(), 1, &SubmitInfo, Frame.m_VKFence ); VKErr )
        {
            m_Device->m_Instance->ReportError(VKErr, "vkQueueSubmit");
            assert(false);
        }

        return nullptr;
    }

    //------------------------------------------------------------------------------------------------------------------------

    void window::setPipelineInstance( xgpu::cmd_buffer& CmdBuffer, xgpu::pipeline_instance& Instance ) noexcept
    {
        auto& CB                = reinterpret_cast<cmdbuffer&>(CmdBuffer.m_Memory);
        auto& PipelineInstance  = static_cast<xgpu::vulkan::pipeline_instance&>( *Instance.m_Private );
        pipeline_instance::per_renderpass PerRenderPass{};

        //
        // Create the render pipeline if we have not done so yet
        //
        if( auto It = m_PipeLineInstanceMap.find(reinterpret_cast<std::uint64_t>(&PipelineInstance) ); It == m_PipeLineInstanceMap.end() )
        {
            const auto& Pipeline           = *PipelineInstance.m_Pipeline;
            auto        PipelineCreateInfo = Pipeline.m_VkPipelineCreateInfo;

            PerRenderPass.m_pPipelineInstance = &PipelineInstance;

            if(Pipeline.m_nVKDescriptorSetLayout)
            {
                //
                // Create Descriptor Set:
                //
                {
                    std::lock_guard Lk(m_Device->m_LockedVKDescriptorPool);

                    VkDescriptorSetAllocateInfo AllocInfo
                    { .sType                = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
                    , .descriptorPool       = m_Device->m_LockedVKDescriptorPool.get()
                    , .descriptorSetCount   = static_cast<std::uint32_t>(Pipeline.m_nVKDescriptorSetLayout)
                    , .pSetLayouts          = Pipeline.m_VKDescriptorSetLayout.data()
                    };

                    if( auto VKErr = vkAllocateDescriptorSets(m_Device->m_VKDevice, &AllocInfo, PerRenderPass.m_VKDescriptorSet.data() ); VKErr)
                    {
                        m_Device->m_Instance->ReportError(VKErr, "vkAllocateDescriptorSets");
                        assert(false);
                    }
                }

                //
                // Setup the textures
                //
                std::array< VkWriteDescriptorSet,  16> WriteDes {};

                //
                // Setup the textures
                //
                std::array< VkDescriptorImageInfo, 16> DescImage;
                int Index = 0;
                for( int i=0; i< PerRenderPass.m_pPipelineInstance->m_Pipeline->m_nSamplers; ++i )
                {
                    DescImage[i]             = PerRenderPass.m_pPipelineInstance->m_TexturesBinds[i]->m_VKDescriptorImageInfo;
                    DescImage[i].sampler     = PerRenderPass.m_pPipelineInstance->m_Pipeline->m_VKSamplers[i];

                    WriteDes[Index].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    WriteDes[Index].pNext            = nullptr;
                    WriteDes[Index].dstSet           = PerRenderPass.m_VKDescriptorSet[0];      // Not matter how many sets we have textures always is in set 0
                    WriteDes[Index].descriptorCount  = 1;
                    WriteDes[Index].descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    WriteDes[Index].dstArrayElement  = 0;
                    WriteDes[Index].pImageInfo       = &DescImage[i];
                    WriteDes[Index].dstBinding       = i;
                    WriteDes[Index].pBufferInfo      = nullptr;
                    WriteDes[Index].pTexelBufferView = nullptr;
                    Index++;
                }

                //
                // Setup the UniformBuffers
                //
                std::array< VkDescriptorBufferInfo, 5> DescUniformBuffer;
                for (int i = 0; i < PerRenderPass.m_pPipelineInstance->m_Pipeline->m_nUniformBuffers; ++i)
                {
                    DescUniformBuffer[i].offset = 0;
                    DescUniformBuffer[i].buffer = PerRenderPass.m_pPipelineInstance->m_UniformBinds[i]->m_VKBuffer;
                    DescUniformBuffer[i].range  = PerRenderPass.m_pPipelineInstance->m_UniformBinds[i]->m_EntrySizeBytes;

                    WriteDes[Index].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    WriteDes[Index].pNext            = nullptr;
                    WriteDes[Index].dstSet           = PerRenderPass.m_VKDescriptorSet[Pipeline.m_nVKDescriptorSetLayout-1];    // Not matter how many sets we have Uniforms always are the last set
                    WriteDes[Index].descriptorCount  = 1;
                    WriteDes[Index].descriptorType   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                    WriteDes[Index].dstArrayElement  = 0;
                    WriteDes[Index].pImageInfo       = nullptr;
                    WriteDes[Index].dstBinding       = i;
                    WriteDes[Index].pBufferInfo      = &DescUniformBuffer[i];
                    WriteDes[Index].pTexelBufferView = nullptr;
                    Index++;
                }

                vkUpdateDescriptorSets  ( m_Device->m_VKDevice
                                        , Index
                                        , WriteDes.data()
                                        , 0
                                        , nullptr 
                                        );
            }

            //
            // See if we have already created this material
            //
            if (auto ItPipeline = m_PipeLineMap.find(reinterpret_cast<std::uint64_t>(CB.m_VKActiveRenderPass) ^ reinterpret_cast<std::uint64_t>(PipelineInstance.m_Pipeline.get())); ItPipeline == m_PipeLineMap.end())
            {
                //
                // Create the pipeline info
                //
                auto VKPipelineCreateInfo = Pipeline.m_VkPipelineCreateInfo;
                VKPipelineCreateInfo.renderPass = CB.m_VKActiveRenderPass;

                const_cast<VkPipelineColorBlendStateCreateInfo*>(VKPipelineCreateInfo.pColorBlendState)->attachmentCount = CB.m_pActiveRenderPass ? CB.m_pActiveRenderPass->m_nColorAttachments : 1u;


                // Create rendering pipeline
                if (auto VKErr = vkCreateGraphicsPipelines
                        ( m_Device->m_VKDevice
                        , m_Device->m_VKPipelineCache
                        , 1
                        , &VKPipelineCreateInfo
                        , m_Device->m_Instance->m_pVKAllocator
                        , &PerRenderPass.m_VKPipeline
                        )
                    ; VKErr
                    )
                {
                    m_Device->m_Instance->ReportError(VKErr, "vkCreateGraphicsPipelines");
                    assert(false);
                }

                //
                // Cache this Pipeline
                //
                pipeline::per_renderpass PipelinePerRP
                { .m_pPipeline  = PipelineInstance.m_Pipeline.get()
                , .m_VKPipeline = PerRenderPass.m_VKPipeline
                };
                m_PipeLineMap.emplace(std::pair{ reinterpret_cast<std::uint64_t>(PipelineInstance.m_Pipeline.get()), PipelinePerRP });
            }
            else
            {
                // Set the pipeline
                PerRenderPass.m_VKPipeline = ItPipeline->second.m_VKPipeline;
            }

            //
            // Cache this instance
            //
            m_PipeLineInstanceMap.emplace( std::pair{ reinterpret_cast<std::uint64_t>(&PipelineInstance), PerRenderPass } );
        }
        else
        {
            PerRenderPass = It->second;
        }

        //
        // Bind pipeline and descriptor set
        //
        {
            vkCmdBindPipeline( CB.m_VKCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PerRenderPass.m_VKPipeline );

            // if we have textures then we must have set zero define for them
            if( PerRenderPass.m_pPipelineInstance->m_TexturesBinds[0].get() )
            {
                // Textures are always in the descriptor set zero
                auto DescriptorSet = std::array{ PerRenderPass.m_VKDescriptorSet[0] };

                vkCmdBindDescriptorSets
                ( CB.m_VKCommandBuffer
                , VK_PIPELINE_BIND_POINT_GRAPHICS
                , PerRenderPass.m_pPipelineInstance->m_Pipeline->m_VKPipelineLayout
                , 0
                , static_cast<std::uint32_t>(DescriptorSet.size())
                , DescriptorSet.data()
                , 0
                , nullptr
                );
            }
        }

        CB.m_ActivePipelineInstance = PerRenderPass;
        CB.m_VKPipelineLayout       = PerRenderPass.m_pPipelineInstance->m_Pipeline->m_VKPipelineLayout;
    }

    //------------------------------------------------------------------------------------------------------------------------

    void window::setBuffer( xgpu::cmd_buffer& CmdBuffer, xgpu::buffer& Buffer, int StartingElementIndex ) noexcept
    {
        auto& VBuffer   = *static_cast<xgpu::vulkan::buffer*>(Buffer.m_Private.get());
        auto& CB        = reinterpret_cast<cmdbuffer&>(CmdBuffer.m_Memory);

        switch( VBuffer.m_Type )
        {
            case xgpu::buffer::type::VERTEX:
            {
                auto VertexBuffers = std::array{ VBuffer.m_VKBuffer };
                auto VertexOffset  = std::array{ static_cast<VkDeviceSize>(StartingElementIndex*VBuffer.m_EntrySizeBytes) };
                static_assert( VertexBuffers.size() == VertexOffset.size() );

                vkCmdBindVertexBuffers
                ( CB.m_VKCommandBuffer
                , 0 // TODO: Binding!!!!!!!!!!!!!!!!!!!!!!!
                , static_cast<std::uint32_t>(VertexBuffers.size())
                , VertexBuffers.data()
                , VertexOffset.data()
                );

                break;
            }
            case xgpu::buffer::type::INDEX:
            {
                vkCmdBindIndexBuffer
                ( CB.m_VKCommandBuffer
                , VBuffer.m_VKBuffer
                , static_cast<VkDeviceSize>(StartingElementIndex * VBuffer.m_EntrySizeBytes)
                , VBuffer.m_EntrySizeBytes == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32
                );

                break;
            }
            default:
                assert(false);
                break;
        }
    }

    //------------------------------------------------------------------------------------------------------------------------

    void window::setStreamingBuffers(xgpu::cmd_buffer& CmdBuffer, std::span<xgpu::buffer> Buffers, int StartingElementIndex ) noexcept
    {
        auto iStartBuffer = 0u;
        {
            auto& VBuffer = *static_cast<xgpu::vulkan::buffer*>(Buffers[iStartBuffer].m_Private.get());
            if (VBuffer.m_Type == xgpu::buffer::type::INDEX)
            {
                setBuffer(CmdBuffer, Buffers[iStartBuffer], StartingElementIndex);
                iStartBuffer++;
            }
        }

        std::array<VkBuffer,16>         VKBuffers   {};
        std::array<VkDeviceSize,16>     Offsets     {};
        for( std::uint32_t i=iStartBuffer, end_i = static_cast<std::uint32_t>(Buffers.size()); i!=end_i; ++i )
        {
            auto& VBuffer = *static_cast<xgpu::vulkan::buffer*>(Buffers[i].m_Private.get());
            VKBuffers[i-1] = VBuffer.m_VKBuffer;
        }

        auto& CB = reinterpret_cast<cmdbuffer&>(CmdBuffer.m_Memory);

        vkCmdBindVertexBuffers
        ( CB.m_VKCommandBuffer
        , 0 // Starting binding
        , static_cast<std::uint32_t>(Buffers.size() - iStartBuffer)
        , VKBuffers.data()
        , Offsets.data()
        );
    }

    //------------------------------------------------------------------------------------------------------------------------

    void window::setViewport(xgpu::cmd_buffer& CmdBuffer, float x, float y, float w, float h, float minDepth, float maxDepth) noexcept
    {
        auto& CB = reinterpret_cast<cmdbuffer&>(CmdBuffer.m_Memory);

        auto Viewport = VkViewport
        {  .x           = x
         , .y           = y
         , .width       = w
         , .height      = h
         , .minDepth    = minDepth
         , .maxDepth    = maxDepth
        };

        auto ViewportArray = std::array{ Viewport };
        vkCmdSetViewport
        ( CB.m_VKCommandBuffer
        , 0
        , static_cast<std::uint32_t>(ViewportArray.size())
        , ViewportArray.data()
        );
    }

    //------------------------------------------------------------------------------------------------------------------------

    void window::setScissor(xgpu::cmd_buffer& CmdBuffer, int x, int y, int w, int h )  noexcept
    {
        VkRect2D Scissor;

        // Negative offsets are illegal for vkCmdSetScissor
        if (x < 0) x = 0;
        if (y < 0) y = 0;

        assert(w>=0);
        assert(h>=0);

        Scissor.offset.x       = x;
        Scissor.offset.y       = y;
        Scissor.extent.width   = w;
        Scissor.extent.height  = h;

        auto& CB = reinterpret_cast<cmdbuffer&>(CmdBuffer.m_Memory);

        auto ScissorArray = std::array{ Scissor };
        vkCmdSetScissor
        ( CB.m_VKCommandBuffer
        , 0
        , static_cast<std::uint32_t>(ScissorArray.size())
        , ScissorArray.data()
        );
    }

    //------------------------------------------------------------------------------------------------------------------------

    void window::DrawInstance(xgpu::cmd_buffer& CmdBuffer, int InstanceCount, int IndexCount, int FirstInstance, int FirstIndex, int VertexOffset) noexcept
    {
        auto& CB    = reinterpret_cast<cmdbuffer&>(CmdBuffer.m_Memory);

        //
        // Issue the draw call
        //
        vkCmdDrawIndexed
        ( CB.m_VKCommandBuffer
        , IndexCount
        , InstanceCount
        , FirstIndex
        , VertexOffset
        , FirstInstance 
        );
    }

    //------------------------------------------------------------------------------------------------------------------------

    void window::setPushConstants(xgpu::cmd_buffer& CmdBuffer, const void* pData, std::size_t Size ) noexcept
    {
        auto& CB    = reinterpret_cast<cmdbuffer&>(CmdBuffer.m_Memory);

        vkCmdPushConstants
        ( CB.m_VKCommandBuffer
        , CB.m_VKPipelineLayout
        , VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT 
        , 0
        , static_cast<std::uint32_t>(Size)
        , pData
        );
    }

    //------------------------------------------------------------------------------------------------------------------------

    void* window::getUniformBufferVMem(xgpu::cmd_buffer& CmdBuffer, xgpu::shader::type::bit ShaderType, std::size_t Size ) noexcept
    {
        auto& CB = reinterpret_cast<cmdbuffer&>(CmdBuffer.m_Memory);
        
        std::uint32_t DynamicOffset;
        void* pData = CB.m_ActivePipelineInstance.m_pPipelineInstance->getUniformBufferVMem(DynamicOffset, ShaderType, Size);

        // Uniform Buffers are always in the last descriptor set
        auto DescriptorSet      = std::array{ CB.m_ActivePipelineInstance.m_VKDescriptorSet[1] ? CB.m_ActivePipelineInstance.m_VKDescriptorSet[1] : CB.m_ActivePipelineInstance.m_VKDescriptorSet[0] };
        auto DynamicOffsetList  = std::array<std::uint32_t,1>{DynamicOffset};

        vkCmdBindDescriptorSets
        ( CB.m_VKCommandBuffer
        , VK_PIPELINE_BIND_POINT_GRAPHICS
        , CB.m_VKPipelineLayout
        , 1
        , static_cast<std::uint32_t>(DescriptorSet.size())
        , DescriptorSet.data()
        , static_cast<std::uint32_t>(DynamicOffsetList.size())
        , DynamicOffsetList.data()
        );

        return pData;
    }

    //------------------------------------------------------------------------------------------------------------------------

    void window::setDepthBias(xgpu::cmd_buffer& CmdBuffer, float ConstantFactor, float DepthBiasClamp, float DepthBiasSlope) noexcept
    {
        auto& CB = reinterpret_cast<cmdbuffer&>(CmdBuffer.m_Memory);
        vkCmdSetDepthBias
        ( CB.m_VKCommandBuffer
        , ConstantFactor
        , DepthBiasClamp
        , DepthBiasSlope 
        );
    }

    //------------------------------------------------------------------------------------------------------------------------
    void window::setClearColor(float R, float G, float B, float A)  noexcept
    {
        m_VKClearValue[0] = VkClearValue{ .color = {.float32 = {R,G,B,A}}};
    }

}