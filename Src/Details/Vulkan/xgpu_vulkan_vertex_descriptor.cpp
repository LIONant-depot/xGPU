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
                case xgpu::vertex_descriptor::format::FLOAT_1D:                return VK_FORMAT_R32_SFLOAT;
                case xgpu::vertex_descriptor::format::FLOAT_2D:                return VK_FORMAT_R32G32_SFLOAT;
                case xgpu::vertex_descriptor::format::FLOAT_3D:                return VK_FORMAT_R32G32B32_SFLOAT;
                case xgpu::vertex_descriptor::format::FLOAT_4D:                return VK_FORMAT_R32G32B32A32_SFLOAT;
                case xgpu::vertex_descriptor::format::UINT8_1D_NORMALIZED:    return VK_FORMAT_R8_UNORM;
                case xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED:    return VK_FORMAT_R8G8B8A8_UNORM;
                case xgpu::vertex_descriptor::format::UINT8_1D:               return VK_FORMAT_R8_UINT;
                case xgpu::vertex_descriptor::format::UINT16_1D:              return VK_FORMAT_R16_UINT;
                case xgpu::vertex_descriptor::format::UINT32_1D:              return VK_FORMAT_R32_UINT;
                case xgpu::vertex_descriptor::format::SINT8_3D_NORMALIZED:    return VK_FORMAT_R8G8B8_SNORM;
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