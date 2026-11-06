namespace xgpu::vulkan
{
    // Compatibility identity of a render pass (formats, samples, subpass attachment indices,
    // dependencies). Load/store ops and layouts are not included: two compatible render passes
    // may share a VkPipeline (Vulkan spec, "Render Pass Compatibility").
    inline std::uint64_t RenderPassCompatKey( const VkRenderPassCreateInfo& Info ) noexcept
    {
        std::uint64_t h = 14695981039346656037ull;
        h = HashMix( h, Info.attachmentCount );
        h = HashMix( h, Info.subpassCount );
        h = HashMix( h, Info.dependencyCount );
        for( std::uint32_t i = 0; i < Info.attachmentCount; ++i )
        {
            const auto& A = Info.pAttachments[i];
            h = HashMix( h, A.format );
            h = HashMix( h, A.samples );
        }
        auto MixRefs = [&]( const VkAttachmentReference* Refs, std::uint32_t n, bool Present )
        {
            if( !Present || Refs == nullptr )
            {
                h = HashMix( h, ~0ull );
                return;
            }
            for( std::uint32_t j = 0; j < n; ++j )
                h = HashMix( h, Refs[j].attachment );
        };
        for( std::uint32_t i = 0; i < Info.subpassCount; ++i )
        {
            const auto& S = Info.pSubpasses[i];
            h = HashMix( h, S.pipelineBindPoint );
            h = HashMix( h, S.flags );
            h = HashMix( h, S.colorAttachmentCount );
            h = HashMix( h, S.inputAttachmentCount );
            h = HashMix( h, S.preserveAttachmentCount );
            MixRefs( S.pColorAttachments, S.colorAttachmentCount, true );
            MixRefs( S.pResolveAttachments, S.colorAttachmentCount, S.pResolveAttachments != nullptr );
            MixRefs( S.pDepthStencilAttachment, 1, S.pDepthStencilAttachment != nullptr );
            MixRefs( S.pInputAttachments, S.inputAttachmentCount, true );
            if( S.pPreserveAttachments )
            {
                for( std::uint32_t j = 0; j < S.preserveAttachmentCount; ++j )
                    h = HashMix( h, S.pPreserveAttachments[j] );
            }
        }
        for( std::uint32_t i = 0; i < Info.dependencyCount; ++i )
        {
            const auto& D = Info.pDependencies[i];
            h = HashMix( h, D.srcSubpass );
            h = HashMix( h, D.dstSubpass );
            h = HashMix( h, D.srcStageMask );
            h = HashMix( h, D.dstStageMask );
            h = HashMix( h, D.srcAccessMask );
            h = HashMix( h, D.dstAccessMask );
            h = HashMix( h, D.dependencyFlags );
        }
        return h ? h : 1;
    }

    struct renderpass final : xgpu::details::renderpass_handle
    {
        xgpu::device::error* Initialize ( std::shared_ptr<vulkan::device>&& Device
                                        , const xgpu::renderpass::setup&    Setup
                                        ) noexcept;

        virtual void DeathMarch(xgpu::renderpass&& RenderPass) noexcept override;
        virtual ~renderpass(void) noexcept override;

        using clear_values_array = std::array<VkClearValue, xgpu::renderpass::max_attachments_v>;

        VkRenderPassBeginInfo                               m_VKRenderPassBeginInfo {};
        std::uint32_t                                       m_nColorAttachments     {};
        clear_values_array                                  m_ClearValues           {};
        VkRenderPass                                        m_VKRenderPass          {};
        std::uint64_t                                       m_RenderPassCompatKey   {};
        VkFramebuffer                                       m_VKFrameBuffer         {};
        std::shared_ptr<vulkan::device>                     m_Device                {};
        std::array< std::shared_ptr<vulkan::texture>, 16>   m_TextureAttachments    {};
                                            
    };
}
