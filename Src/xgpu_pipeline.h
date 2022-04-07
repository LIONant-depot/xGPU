namespace xgpu
{
    struct pipeline
    {
        struct primitive
        {
            enum class raster : std::uint8_t
            {
                FILL
            ,   WIRELINE
            ,   POINT
            ,   ENUM_COUNT
            };

            enum class front_face : std::uint8_t
            {
                COUNTER_CLOCKWISE
            ,   CLOCKWISE
            ,   ENUM_COUNT
            };

            enum class cull : std::uint8_t
            {
                NONE
            ,   FRONT
            ,   BACK
            ,   ALL
            ,   ENUM_COUNT
            };

            float           m_LineWidth                     {1};                                // is the width of rasterized line segments
            raster          m_Raster                        { raster::FILL };
            front_face      m_FrontFace                     { front_face::COUNTER_CLOCKWISE };
            cull            m_Cull                          { cull::BACK };
            bool            m_bRasterizerDiscardEnable      { false };                          // Controls whether primitives are discarded immediately before the rasterization stage.
        };

        // Blend Equation: Screen.color = ( SourceColor.rgb * m_ColorSrcFactor ) m_ColorOperation ( DestinationColor.rgb * m_ColorDstFactor )
        // Blend Equation: Screen.alpha = ( SourceColor.a   * m_AlphaSrcFactor ) m_AlphaOperation ( DestinationColor.a   * m_AlphaDstFactor )
        struct blend
        {
            enum class factor : std::uint8_t
            {
                ZERO
            ,   ONE
            ,   SRC_COLOR
            ,   ONE_MINUS_SRC_COLOR
            ,   DST_COLOR
            ,   ONE_MINUS_DST_COLOR
            ,   SRC_ALPHA
            ,   ONE_MINUS_SRC_ALPHA
            ,   DST_ALPHA
            ,   ONE_MINUS_DST_ALPHA
            ,   CONSTANT_COLOR
            ,   ONE_MINUS_CONSTANT_COLOR
            ,   CONSTANT_ALPHA
            ,   ONE_MINUS_CONSTANT_ALPHA
            ,   SRC_ALPHA_SATURATE
            ,   SRC1_COLOR
            ,   ONE_MINUS_SRC1_COLOR
            ,   SRC1_ALPHA
            ,   ONE_MINUS_SRC1_ALPHA
            ,   ENUM_COUNT
            };

            enum class op : std::uint8_t
            {
                ADD
            ,   SUBTRACT
            ,   REVERSE_SUBTRACT
            ,   MIN
            ,   MAX
            ,   ENUM_COUNT
            };

            constexpr static   blend    getBlendOff           ( void ) noexcept;
            constexpr static   blend    getAlphaOriginal      ( void ) noexcept;
            constexpr static   blend    getAlphaPreMultiply   ( void ) noexcept;
            constexpr static   blend    getMuliply            ( void ) noexcept;
            constexpr static   blend    getAdd                ( void ) noexcept;
            constexpr static   blend    getSubSrcFromDest     ( void ) noexcept;
            constexpr static   blend    getSubDestFromSrc     ( void ) noexcept;

            std::uint8_t    m_ColorWriteMask    { 0b1111 };       // One bit per component R,G,B,A
            bool            m_bEnable           { false };

            // SrcC * SrcFactor  (Op)  DesC * DscFactor
            factor          m_ColorSrcFactor    { factor::ONE };
            factor          m_ColorDstFactor    { factor::ZERO };
            op              m_ColorOperation    { op::ADD };

            factor          m_AlphaSrcFactor    { factor::ZERO };
            factor          m_AlphaDstFactor    { factor::ZERO };
            op              m_AlphaOperation    { op::ADD };
        };

        struct depth_stencil
        {
            enum class depth_compare : std::uint8_t
            {
                LESS
            ,   LESS_OR_EQUAL
            ,   GREATER
            ,   NOT_EQUAL
            ,   GREATER_OR_EQUAL
            ,   EQUAL
            ,   NEVER
            ,   ALWAYS
            ,   ENUM_COUNT
            };

            enum class stencil_op : std::uint8_t
            {
                KEEP
            ,   ZERO
            ,   REPLACE
            ,   INCREMENT_AND_CLAMP
            ,   DECREMENT_AND_CLAMP
            ,   INVERT
            ,   INCREMENT_AND_WRAP
            ,   DECREMENT_AND_WRAP
            ,   ENUM_COUNT
            };

            struct stencil_operation_state
            {
                std::uint32_t           m_CompareMask           { ~0u };
                std::uint32_t           m_WriteMask             { ~0u };
                std::uint32_t           m_Reference             { 0u };         // is an integer reference value that is used in the unsigned stencil comparison
                stencil_op              m_FailOp                { stencil_op::KEEP };
                stencil_op              m_PassOp                { stencil_op::KEEP };
                stencil_op              m_DepthFailOp           { stencil_op::KEEP };
                depth_compare           m_CompareOp             { depth_compare::ALWAYS };
            };

            float                       m_DepthMaxBounds            { 1.0f };
            float                       m_DepthMinBounds            { 0 };

            float                       m_DepthBiasConstantFactor   { 0 };      // is a scalar factor controlling the constant depth value added to each fragment
            float                       m_DepthBiasClamp            { 0 };      // is the maximum (or minimum) depth bias of a fragment
            float                       m_DepthBiasSlopeFactor      { 0 };      // is a scalar factor applied to a fragments slope in depth bias calculations

            stencil_operation_state     m_StencilFrontFace          {};
            stencil_operation_state     m_StencilBackFace           {};
            depth_compare               m_DepthCompare              { depth_compare::LESS_OR_EQUAL };

            bool                        m_bDepthTestEnable          { true };
            bool                        m_bDepthWriteEnable         { true };
            bool                        m_bDepthBiasEnable          { false };
            bool                        m_bDepthClampEnable         { false };                        // Clamp the fragments depth values instead of clipping by FarZ
            bool                        m_bDepthBoundsTestEnable    { false };

            bool                        m_StencilTestEnable         { false };
        };

        struct sampler
        {
            enum class address_mode : std::uint8_t
            {
                TILE
            ,   CLAMP
            ,   CLAMP_BORDER
            ,   MIRROR
            ,   MIRROR_CLAMP
            ,   ENUM_COUNT
            };

            enum class mipmap_sampler : std::uint8_t
            {
                LINEAR
            ,   NEAREST
            ,   ENUM_COUNT
            };

            enum class mipmap_mode : std::uint8_t
            {
                LINEAR
            ,   NEAREST
            ,   ENUM_COUNT
            };

            float                       m_MaxAnisotropy         { 8.0f  };
            bool                        m_bDisableAnisotropy    { false };
            std::array<address_mode,3>  m_AddressMode           { address_mode::TILE, address_mode::TILE, address_mode::TILE };
            mipmap_sampler              m_MipmapMag             { mipmap_sampler::LINEAR };
            mipmap_sampler              m_MipmapMin             { mipmap_sampler::LINEAR };
            mipmap_mode                 m_MipmapMode            { mipmap_mode::LINEAR    };
        };

        struct setup
        {
            xgpu::vertex_descriptor&            m_VertexDescriptor;
            std::span<const shader*>            m_Shaders           {};
            std::size_t                         m_PushConstantsSize {};
            std::span<xgpu::shader::type>       m_UniformBufferUsage{};
            std::span<const pipeline::sampler>  m_Samplers          {};
            pipeline::primitive                 m_Primitive         {};
            pipeline::depth_stencil             m_DepthStencil      {};
            pipeline::blend                     m_Blend             {};
        };

        std::shared_ptr<details::pipeline_handle> m_Private {};
    };
}
