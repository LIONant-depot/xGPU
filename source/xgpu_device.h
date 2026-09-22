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
        XGPU_INLINE [[nodiscard]] device::error* Create         ( window&                           Window
                                                                , const window::setup&              Setup 
                                                                ) noexcept;

        XGPU_INLINE [[nodiscard]] device::error* Create         ( renderpass&                       Renderpass
                                                                , const renderpass::setup&          Setup 
                                                                ) noexcept;

        XGPU_INLINE [[nodiscard]] device::error* Create         ( pipeline&                         Pipeline
                                                                , const pipeline::setup&            Setup 
                                                                ) noexcept;

        XGPU_INLINE [[nodiscard]] device::error* Create         ( pipeline_instance&                PipelineInstance
                                                                , const pipeline_instance::setup&   Setup 
                                                                ) noexcept;

        XGPU_INLINE [[nodiscard]] device::error* Create         ( shader&                           Shader
                                                                , const shader::setup&              Setup 
                                                                ) noexcept;

        XGPU_INLINE [[nodiscard]] device::error* Create         ( vertex_descriptor&                VDescriptor
                                                                , const vertex_descriptor::setup&   Setup 
                                                                ) noexcept;

        XGPU_INLINE [[nodiscard]] device::error* Create         ( texture&                          Texture
                                                                , const texture::setup&             Setup 
                                                                ) noexcept;

        XGPU_INLINE [[nodiscard]] device::error* Create         ( buffer&                           Buffer
                                                                , const buffer::setup&              Setup
                                                                ) noexcept;

        // Writes Source into the [OffsetX,OffsetY]..[+Width,+Height] sub-rectangle of Texture's mip 0 (only -
        // no mips/array layers/cubemaps), leaving the rest of the texture untouched. Texture must already be
        // created (with the same format as Source implies) and not currently in use by an in-flight command
        // buffer. General-purpose - any atlas or streaming/dynamic texture content, not tied to one feature.
        XGPU_INLINE [[nodiscard]] device::error* UpdateTexture  ( texture&                          Texture
                                                                , int                                OffsetX
                                                                , int                                OffsetY
                                                                , int                                Width
                                                                , int                                Height
                                                                , std::span<const std::byte>        Source
                                                                ) noexcept;

        // Reads back Texture's mip 0 as packed 32-bit pixels, one uint32 per texel in whatever byte order
        // Texture's own format actually uses (no conversion - e.g. R8G8B8A8_UNORM comes back R in the low
        // byte; the swapchain's own format, which window::Screenshot reads, happens to be B8G8R8A8, but that
        // is not true of every texture format). Only meaningful for an uncompressed 8-bit-per-channel format.
        // Synchronous: Texture's GPU work must already be complete (e.g. a fence already waited on) before
        // calling this, there is no internal wait.
        XGPU_INLINE [[nodiscard]] device::error* ReadTexture    ( const texture&                    Texture
                                                                , std::vector<std::uint32_t>&       Dest
                                                                , int&                               Width
                                                                , int&                               Height
                                                                ) noexcept;

        XGPU_INLINE void                         Destroy        ( pipeline_instance&&               PipelineInstance ) noexcept;
        XGPU_INLINE void                         Destroy        ( pipeline&&                        Pipeline )         noexcept;
        XGPU_INLINE void                         Destroy        ( texture&&                         Texture )          noexcept;
        XGPU_INLINE void                         Destroy        ( buffer&&                          Buffer )           noexcept;
        XGPU_INLINE void                         Destroy        ( window&&                          Window )           noexcept;
        XGPU_INLINE void                         Destroy        ( renderpass&&                      Renderpass )       noexcept;
        XGPU_INLINE void                         Shutdown       ( void ) noexcept;

        std::shared_ptr<details::device_handle>   m_Private;
    };
}