namespace xgpu
{
    struct vertex_descriptor
    {
        enum class format : std::uint8_t
        {   FLOAT_1D
        ,   FLOAT_2D
        ,   FLOAT_3D
        ,   FLOAT_4D
        ,   UINT8_4D
        ,   SINT8_4D
        ,   UINT8_1D_NORMALIZED
        ,   UINT8_4D_NORMALIZED
        ,   UINT8_1D
        ,   UINT16_1D
        ,   SINT16_1D
        ,   UINT32_1D
        ,   SINT8_3D_NORMALIZED
        ,   SINT8_4D_NORMALIZED
        ,   UINT8_4D_UINT
        ,   SINT16_2D
        ,   UINT16_2D
        ,   UINT16_3D
        ,   SINT16_3D
        ,   UINT16_4D
        ,   SINT16_4D
        ,   UINT16_4D_NORMALIZED
        ,   SINT16_4D_NORMALIZED
        ,   UINT16_3D_NORMALIZED
        ,   SINT16_3D_NORMALIZED
        ,   UINT16_2D_NORMALIZED
        ,   SINT16_2D_NORMALIZED
        ,   SINT_RGB10A2_4D_NORMALIZED
        ,   UINT_RGB10A2_4D_NORMALIZED
        ,   ENUM_COUNT
        };

        struct attribute
        {
            std::uint32_t       m_Offset;
            format              m_Format;
            int                 m_iStream;
            bool                m_bPerInstance = false;     // the stream of this attribute advances once per instance (DrawInstance), not once per vertex: every attribute of a stream must agree
        };

        enum class topology : std::uint8_t
        {
            TRIANGLE_LIST
        ,   POINT_LIST
        ,   LINE_LIST
        };

        struct setup
        {
            bool                    m_bUseStreaming = false;
            topology                m_Topology      { topology::TRIANGLE_LIST };
            std::uint32_t           m_VertexSize    {0};
            std::span<attribute>    m_Attributes    {};
            bool                    m_bDynamicStride = false;   // streaming only (Vulkan 1.3): each stream's stride is given when it is bound, so a
                                                                // stream bound as constant (cmd_buffer::setStreamingBuffers' ConstantStreams) has stride 0
                                                                // and every vertex reads its first element - an optional stream the geometry lacks
        };

        std::shared_ptr<details::vertex_descriptor_handle> m_Private;
    };
}