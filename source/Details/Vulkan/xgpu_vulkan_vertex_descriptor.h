namespace xgpu::vulkan
{
    struct vertex_descriptor : xgpu::details::vertex_descriptor_handle
    {
        xgpu::device::error*    Initialize  ( const xgpu::vertex_descriptor::setup& Setup
                                            ) noexcept;

        VkPrimitiveTopology                              m_VKTopology                   {};
        VkPipelineVertexInputStateCreateInfo             m_VKInputStageCreateInfo       {};
        std::array<VkVertexInputBindingDescription,16>   m_VKInputBindingDescription    {};
        std::array<VkVertexInputAttributeDescription,16> m_VKInputAttributesDescription {};
    };
}