namespace xgpu
{
    struct vertex_descriptor
    {
        enum class format : std::uint8_t
        {   FLOAT
        ,   FLOAT2
        ,   FLOAT3
        ,   FLOAT4
        ,   UINT8_RGBA_NORMALIZED
        };

        struct attribute
        {
            std::uint32_t       m_Offset;
            format              m_Format;
        };

        enum class topology : std::uint8_t
        {
            TRIANGLE_LIST
        ,   POINT_LIST
        ,   LINE_LIST
        };

        struct setup
        {
            topology                m_Topology   { topology::TRIANGLE_LIST };
            std::uint32_t           m_VertexSize {0};
            std::span<attribute>    m_Attributes {};
        };

        std::shared_ptr<details::vertex_descriptor_handle> m_Private;
    };
}