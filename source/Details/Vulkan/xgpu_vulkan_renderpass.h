namespace xgpu::vulkan
{
    struct renderpass final : xgpu::details::renderpass_handle
    {
        xgpu::device::error* Initialize ( std::shared_ptr<vulkan::device>&& Device
                                        , const xgpu::renderpass::setup&    Setup
                                        ) noexcept;


        virtual ~renderpass(void) noexcept override;

        using clear_values_array = std::array<VkClearValue, xgpu::renderpass::max_attachments_v>;

        VkRenderPassBeginInfo                               m_VKRenderPassBeginInfo {};
        std::uint32_t                                       m_nColorAttachments     {};
        clear_values_array                                  m_ClearValues           {};
        VkRenderPass                                        m_VKRenderPass          {};
        VkFramebuffer                                       m_VKFrameBuffer         {};
        std::shared_ptr<vulkan::device>                     m_Device                {};
        std::array< std::shared_ptr<vulkan::texture>, 16>   m_TextureAttachments    {};
    };
}
