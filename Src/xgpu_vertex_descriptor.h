namespace xgpu
{
    struct vertex_descriptor
    {
        enum class format : std::uint8_t
        {   FLOAT_1D
        ,   FLOAT_2D
        ,   FLOAT_3D
        ,   FLOAT_4D
        ,   UINT8_1D_NORMALIZED
        ,   UINT8_4D_NORMALIZED
        ,   UINT8_1D
        ,   UINT16_1D
        ,   UINT32_1D
        ,   SINT8_3D_NORMALIZED
        ,   SINT8_4D_NORMALIZED
        ,   UINT8_4D_UINT
        ,   UINT16_4D_NORMALIZED
        ,   SINT16_4D_NORMALIZED
        ,   UINT16_3D_NORMALIZED
        ,   SINT16_3D_NORMALIZED
        ,   UINT16_2D_NORMALIZED
        ,   SINT16_2D_NORMALIZED
        ,   ENUM_COUNT
        };

        struct attribute
        {
            std::uint32_t       m_Offset;
            format              m_Format;
            int                 m_iStream;
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
        };

        std::shared_ptr<details::vertex_descriptor_handle> m_Private;
    };
}