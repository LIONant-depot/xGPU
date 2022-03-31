namespace xgpu
{
    struct device
    {
        enum class error : std::uint8_t
        {
            FAILURE
        };

        enum class type : std::uint8_t
        {
            RENDER_AND_SWAP
        ,   RENDER_ONLY
        ,   COMPUTE
        ,   COPY
        ,   ENUM_COUNT
        };

        enum class discreate : std::uint8_t
        {
            ANY_GPU
        ,   DISCREATE_ONLY
        ,   NON_DISCREATE_ONLY
        };

        struct setup
        {
            type        m_Type              { type::RENDER_AND_SWAP };
            discreate   m_Discreate         { discreate::DISCREATE_ONLY };
        };

        XGPU_INLINE               void           getInstance    ( xgpu::instance& Instance 
                                                                ) const noexcept;
        XGPU_INLINE [[nodiscard]] device::error* Create         ( window&                            Window
                                                                , const window::setup&               Setup 
                                                                ) noexcept;

        XGPU_INLINE [[nodiscard]] device::error* Create         ( renderpass&                        Renderpass
                                                                , const renderpass::setup&           Setup 
                                                                ) noexcept;

        XGPU_INLINE [[nodiscard]] device::error* Create         ( pipeline&                          Pipeline
                                                                , const pipeline::setup&             Setup 
                                                                ) noexcept;

        XGPU_INLINE [[nodiscard]] device::error* Create         ( pipeline_instance&                 PipelineInstance
                                                                , const pipeline_instance::setup&    Setup 
                                                                ) noexcept;

        XGPU_INLINE [[nodiscard]] device::error* Create         ( shader&                            Shader
                                                                , const shader::setup&               Setup 
                                                                ) noexcept;

        XGPU_INLINE [[nodiscard]] device::error* Create         ( vertex_descriptor&                 VDescriptor
                                                                , const vertex_descriptor::setup&    Setup 
                                                                ) noexcept;

        XGPU_INLINE [[nodiscard]] device::error* Create         ( texture&                           Texture
                                                                , const texture::setup&              Setup 
                                                                ) noexcept;

        XGPU_INLINE [[nodiscard]] device::error* Create         ( buffer&                            Buffer
                                                                , const buffer::setup&               Setup 
                                                                ) noexcept;

        std::shared_ptr<details::device_handle>   m_Private;
    };
}