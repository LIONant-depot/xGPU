namespace xgpu::vulkan
{
    // Other intersting material to read:
    // Good overview: https://www.youtube.com/watch?v=mnKp501RXDc 
    // https://developer.android.com/games/optimize/vertex-data-management 
    // https://vulkan.lunarg.com/doc/view/1.2.182.0/windows/chunked_spec/chap22.html
    // https://anteru.net/blog/2016/storing-vertex-data-to-interleave-or-not-to-interleave/
    // https://www.yosoygames.com.ar/wp/2018/03/vertex-formats-part-1-compression/
    //-------------------------------------------------------------------------------------------

    xgpu::device::error* vertex_descriptor::Initialize
    ( const xgpu::vertex_descriptor::setup& Setup
    ) noexcept
    {
        //
        // Make sure the structures are clear
        //
        for (int i = 0; i < Setup.m_Attributes.size(); ++i)
        {
            m_VKInputBindingDescription[i] = VkVertexInputBindingDescription
            { .binding   = std::uint32_t(i)
            , .stride    = 0
            , .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
            };
        }

        //
        // Set attributes
        //
        std::uint32_t nBindings = 0;
        for (int i = 0; i < Setup.m_Attributes.size(); ++i)
        {
            auto& E     = Setup.m_Attributes[i];
            auto Format = [](auto Format) constexpr
            {
                switch (Format)
                {
                case xgpu::vertex_descriptor::format::FLOAT_1D:               return std::pair{ VK_FORMAT_R32_SFLOAT,           4 * 1 };
                case xgpu::vertex_descriptor::format::FLOAT_2D:               return std::pair{ VK_FORMAT_R32G32_SFLOAT,        4 * 2 };
                case xgpu::vertex_descriptor::format::FLOAT_3D:               return std::pair{ VK_FORMAT_R32G32B32_SFLOAT,     4 * 3 };
                case xgpu::vertex_descriptor::format::FLOAT_4D:               return std::pair{ VK_FORMAT_R32G32B32A32_SFLOAT,  4 * 4 };
                case xgpu::vertex_descriptor::format::UINT8_1D_NORMALIZED:    return std::pair{ VK_FORMAT_R8_UNORM,             1 * 1 };
                case xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED:    return std::pair{ VK_FORMAT_R8G8B8A8_UNORM,       1 * 4 };
                case xgpu::vertex_descriptor::format::UINT8_1D:               return std::pair{ VK_FORMAT_R8_UINT,              1 * 1 };
                case xgpu::vertex_descriptor::format::UINT16_1D:              return std::pair{ VK_FORMAT_R16_UINT,             2 * 1 };
                case xgpu::vertex_descriptor::format::UINT32_1D:              return std::pair{ VK_FORMAT_R32_UINT,             4 * 1 };
                case xgpu::vertex_descriptor::format::SINT8_3D_NORMALIZED:    return std::pair{ VK_FORMAT_R8G8B8_SNORM,         1 * 3 };
                case xgpu::vertex_descriptor::format::SINT8_4D_NORMALIZED:    return std::pair{ VK_FORMAT_R8G8B8A8_SNORM,       1 * 4 };
                case xgpu::vertex_descriptor::format::UINT8_4D_UINT:          return std::pair{ VK_FORMAT_R8G8B8A8_UINT,        1 * 4 };
                case xgpu::vertex_descriptor::format::UINT16_4D_NORMALIZED:   return std::pair{ VK_FORMAT_R16G16B16A16_UNORM,   2 * 4 };
                case xgpu::vertex_descriptor::format::SINT16_4D_NORMALIZED:   return std::pair{ VK_FORMAT_R16G16B16A16_SNORM,   2 * 4 };
                case xgpu::vertex_descriptor::format::UINT16_3D_NORMALIZED:   return std::pair{ VK_FORMAT_R16G16B16_UNORM,      2 * 3 };
                case xgpu::vertex_descriptor::format::SINT16_3D_NORMALIZED:   return std::pair{ VK_FORMAT_R16G16B16_SNORM,      2 * 3 };
                case xgpu::vertex_descriptor::format::UINT16_2D_NORMALIZED:   return std::pair{ VK_FORMAT_R16G16_UNORM,         2 * 2 };
                case xgpu::vertex_descriptor::format::SINT16_2D_NORMALIZED:   return std::pair{ VK_FORMAT_R16G16_SNORM,         2 * 2 };
                }

                assert(false);
                return std::pair{ VK_FORMAT_R32G32_SFLOAT, 0 };
            }(E.m_Format);

            m_VKInputAttributesDescription[i].location = i;
            m_VKInputAttributesDescription[i].binding  = E.m_iStream; //Setup.m_bUseStreaming ? E.m_iStream : 0;
            m_VKInputAttributesDescription[i].offset   = E.m_Offset;
            m_VKInputAttributesDescription[i].format   = Format.first;

            m_VKInputBindingDescription[E.m_iStream].stride += Format.second;
            nBindings = std::max(nBindings, static_cast<std::uint32_t>(E.m_iStream + 1) );
        }

        if( !Setup.m_bUseStreaming )
        {
            m_VKInputBindingDescription[nBindings-1] = VkVertexInputBindingDescription
            { .binding    = nBindings - 1
            , .stride     = Setup.m_VertexSize
            , .inputRate  = VK_VERTEX_INPUT_RATE_VERTEX
            };
        }

        m_VKInputStageCreateInfo = VkPipelineVertexInputStateCreateInfo
        { .sType                              = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
        , .vertexBindingDescriptionCount      = nBindings
        , .pVertexBindingDescriptions         = m_VKInputBindingDescription.data()
        , .vertexAttributeDescriptionCount    = static_cast<std::uint32_t>(Setup.m_Attributes.size())
        , .pVertexAttributeDescriptions       = m_VKInputAttributesDescription.data()
        };

        // TODO: Don't hardcore this
        m_VKTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        return nullptr;
    }
}