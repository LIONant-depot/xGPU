namespace xgpu::vulkan
{
    //-------------------------------------------------------------------------------------------

    xgpu::device::error* vertex_descriptor::Initialize
    ( const xgpu::vertex_descriptor::setup& Setup
    ) noexcept
    {
        std::uint32_t i=0;
        for( const auto& E : Setup.m_Attributes )
        {
            m_VKInputAttributesDescription[i].location = i;
            m_VKInputAttributesDescription[i].binding  = m_VKInputBindingDescription[0].binding;
            m_VKInputAttributesDescription[i].offset   = E.m_Offset;
            m_VKInputAttributesDescription[i].format   = [](auto Format) constexpr
            {
                switch (Format)
                {
                case xgpu::vertex_descriptor::format::FLOAT:                    return VK_FORMAT_R32_SFLOAT;
                case xgpu::vertex_descriptor::format::FLOAT2:                   return VK_FORMAT_R32G32_SFLOAT;
                case xgpu::vertex_descriptor::format::FLOAT3:                   return VK_FORMAT_R32G32B32_SFLOAT;
                case xgpu::vertex_descriptor::format::FLOAT4:                   return VK_FORMAT_R32G32B32A32_SFLOAT;
                case xgpu::vertex_descriptor::format::UINT8_RGBA_NORMALIZED:    return VK_FORMAT_R8G8B8A8_UNORM;
                }

                assert(false);
                return VK_FORMAT_R32G32_SFLOAT;
            }(E.m_Format);

            i++;
        }

        m_VKInputBindingDescription[0] = VkVertexInputBindingDescription
        {
            .stride     = Setup.m_VertexSize
        ,   .inputRate  = VK_VERTEX_INPUT_RATE_VERTEX
        };

        m_VKInputStageCreateInfo = VkPipelineVertexInputStateCreateInfo
        {
            .sType                              = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
        ,   .vertexBindingDescriptionCount      = 1
        ,   .pVertexBindingDescriptions         = m_VKInputBindingDescription.data()
        ,   .vertexAttributeDescriptionCount    = i
        ,   .pVertexAttributeDescriptions       = m_VKInputAttributesDescription.data()
        };

        // TODO: Dont hardcore this
        m_VKTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        return nullptr;
    }
}