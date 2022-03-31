namespace xgpu::vulkan
{
    //---------------------------------------------------------------------------------------

    xgpu::device::error* renderpass::Initialize
    ( std::shared_ptr<vulkan::device>&& Device
    , const xgpu::renderpass::setup&    Setup
    ) noexcept
    {
        std::array<VkAttachmentDescription, xgpu::renderpass::max_attachments_v> attachmentDescs{};
        std::array<VkAttachmentReference,   xgpu::renderpass::max_attachments_v> colorReferences{};
        std::array<VkImageView,             xgpu::renderpass::max_attachments_v> attachments{};
        VkAttachmentReference                                                    depthReference{};

        std::uint32_t nAttachments      = 0;
        std::uint32_t nColorAttachments = 0;
        
        std::uint32_t W, H;
        for( auto& Attch : Setup.m_Attachments )
        {
            auto& Texture = *reinterpret_cast<xgpu::vulkan::texture*>(Attch.m_Texture.m_Private.get());

            const auto isDepth = xgpu::texture::isDepth(Texture.m_Format);

            if(nAttachments==0) 
            {
                H = Texture.m_Height;
                W = Texture.m_Width;
            }
            else
            {
                // Simple validation
                if( H != Texture.m_Height || W != Texture.m_Width )
                {
                    m_Device->m_Instance->ReportError( "All attachments for a render pass must have the same resolution");
                    return VGPU_ERROR(xgpu::device::error::FAILURE, "All attachments for a render pass must have the same resolution");
                }
            }

            // Set the view into our attachment list 
            attachments[nAttachments] = Texture.m_VKView;

            // remember which attachment was my depth (if any)
            if(isDepth)
            {
                // we currently only support one depth attachment
                assert(!depthReference.layout);

                depthReference = VkAttachmentReference
                { .attachment       = nAttachments
                , .layout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                };

                m_ClearValues[nAttachments].depthStencil = { Setup.m_ClearDepthFar, Setup.m_ClearStencil };

            }
            else
            {
                m_ClearValues[nAttachments].color = { { Setup.m_ClearColorR, Setup.m_ClearColorG, Setup.m_ClearColorB, Setup.m_ClearColorA} };

                colorReferences[nColorAttachments++] = VkAttachmentReference
                { .attachment = nAttachments
                , .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                };
            }

            attachmentDescs[nAttachments++] = VkAttachmentDescription
            { .format           = Texture.m_VKFormat
            , .samples          = VK_SAMPLE_COUNT_1_BIT
            , .loadOp           = VK_ATTACHMENT_LOAD_OP_CLEAR
            , .storeOp          = VK_ATTACHMENT_STORE_OP_STORE
            , .stencilLoadOp    = VK_ATTACHMENT_LOAD_OP_DONT_CARE
            , .stencilStoreOp   = VK_ATTACHMENT_STORE_OP_DONT_CARE
            , .initialLayout    = VK_IMAGE_LAYOUT_UNDEFINED
            , .finalLayout      = isDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };
        }

        VkSubpassDescription subpass = 
        { .pipelineBindPoint        = VK_PIPELINE_BIND_POINT_GRAPHICS
        , .colorAttachmentCount     = nColorAttachments
        , .pColorAttachments        = colorReferences.data()
        , .pDepthStencilAttachment  = &depthReference
        };

        // Use subpass dependencies for layout transitions
        std::array<VkSubpassDependency, 2> dependencies{};

        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;

        // Are we doing some kind of depth only pass? (Shadow maps, etc)
        if( nColorAttachments == 0 )
        {
            assert(depthReference.layout);

            dependencies[0].srcStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dependencies[0].dstStageMask    = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dependencies[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT;
            dependencies[0].dstAccessMask   = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            dependencies[1].srcStageMask    = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dependencies[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dependencies[1].srcAccessMask   = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            dependencies[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;
            dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        }
        else
        {
            dependencies[0].srcStageMask    = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            dependencies[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[0].srcAccessMask   = VK_ACCESS_MEMORY_READ_BIT;
            dependencies[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            dependencies[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[1].dstStageMask    = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            dependencies[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[1].dstAccessMask   = VK_ACCESS_MEMORY_READ_BIT;
            dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        }

        VkRenderPassCreateInfo renderPassCreateInfo =
        { .sType            = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO
        , .attachmentCount  = nAttachments
        , .pAttachments     = attachmentDescs.data()
        , .subpassCount     = 1
        , .pSubpasses       = &subpass
        , .dependencyCount  = static_cast<std::uint32_t>(dependencies.size())
        , .pDependencies    = dependencies.data()
        };

        if(auto VKErr = vkCreateRenderPass( Device->m_VKDevice, &renderPassCreateInfo, nullptr, &m_VKRenderPass ) )
        {
            m_Device->m_Instance->ReportError(VKErr, "Fail to create a vulkan renderpass");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create a vulkan renderpass");
        }

        //
        // Create the frame buffer
        //
        VkFramebufferCreateInfo framebufCreateInfo = 
        { .sType            = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
        , .pNext            = nullptr
        , .renderPass       = m_VKRenderPass
        , .attachmentCount  = nAttachments
        , .pAttachments     = attachments.data()
        , .width            = W
        , .height           = H
        , .layers           = 1
        };
        if (auto VKErr = vkCreateFramebuffer(Device->m_VKDevice, &framebufCreateInfo, nullptr, &m_VKFrameBuffer))
        {
            m_Device->m_Instance->ReportError(VKErr, "Fail to create a vulkan frame buffer");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create a vulkan frame buffer");
        }

        //
        // Add references
        //
        m_Device            = Device;
        m_nColorAttachments = nColorAttachments;

        // Add references to all the attachments
        int i=0;
        for( auto& Attch : Setup.m_Attachments )
        {
            m_TextureAttachments[i++] = std::reinterpret_pointer_cast<xgpu::vulkan::texture>(Attch.m_Texture.m_Private);
        }

        // Initialize the RenderPassBeginInfo 
        m_VKRenderPassBeginInfo = VkRenderPassBeginInfo
        { .sType                = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
        , .renderPass           = m_VKRenderPass
        , .framebuffer          = m_VKFrameBuffer
        , .renderArea           = {.extent = {.width = W, .height = H }}
        , .clearValueCount      = nAttachments
        , .pClearValues         = m_ClearValues.data()
        };

        return nullptr;
    }

    //---------------------------------------------------------------------------------------

    renderpass::~renderpass(void) noexcept
    {
    
    }
}