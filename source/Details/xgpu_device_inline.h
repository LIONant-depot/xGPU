namespace xgpu
{
    namespace details
    {
        struct device_handle
        {
            virtual                        ~device_handle           ( void ) noexcept = default;

            virtual void                    getInstance             ( xgpu::instance& Instance 
                                                                    ) const noexcept = 0;
            virtual device::error*          Create                  ( xgpu::window&                         Window
                                                                    , const xgpu::window::setup&            Setup
                                                                    , std::shared_ptr<device_handle>&       SharedDevice
                                                                    ) noexcept = 0;
            virtual device::error*          Create                  ( xgpu::renderpass&                     Renderpass
                                                                    , const xgpu::renderpass::setup&        Setup
                                                                    , std::shared_ptr<device_handle>&       SharedDevice
                                                                    ) noexcept = 0;
            virtual device::error*          Create                  ( pipeline&                             Pipeline
                                                                    , const xgpu::pipeline::setup&          Setup
                                                                    , std::shared_ptr<device_handle>&       SharedDevice
                                                                    ) noexcept = 0;
            virtual device::error*          Create                  ( xgpu::pipeline_instance&              PipelineInstance
                                                                    , const xgpu::pipeline_instance::setup& Setup
                                                                    , std::shared_ptr<device_handle>&       SharedDevice
                                                                    ) noexcept = 0;
            virtual device::error*          Create                  ( shader&                               Shader
                                                                    , const xgpu::shader::setup&            Setup
                                                                    , std::shared_ptr<device_handle>&       SharedDevice
                                                                    ) noexcept = 0;
            virtual device::error*          Create                  ( vertex_descriptor&                    VDescriptor
                                                                    , const vertex_descriptor::setup&       Setup
                                                                    , std::shared_ptr<device_handle>&       SharedDevice
                                                                    ) noexcept = 0;
            virtual device::error*          Create                  ( texture&                              Texture
                                                                    , const texture::setup&                 Setup
                                                                    , std::shared_ptr<device_handle>&       SharedDevice
                                                                    ) noexcept = 0;
            virtual device::error*          Create                  ( xgpu::buffer&                         Buffer
                                                                    , const xgpu::buffer::setup&            Setup
                                                                    , std::shared_ptr<device_handle>&       SharedDevice
                                                                    ) noexcept = 0;

            virtual device::error*          UpdateTexture           ( xgpu::texture&                        Texture
                                                                    , int                                    OffsetX
                                                                    , int                                    OffsetY
                                                                    , int                                    Width
                                                                    , int                                    Height
                                                                    , std::span<const std::byte>            Source
                                                                    ) noexcept = 0;

            virtual device::error*          ReadTexture             ( const xgpu::texture&                  Texture
                                                                    , std::vector<std::uint32_t>&           Dest
                                                                    , int&                                   Width
                                                                    , int&                                   Height
                                                                    ) noexcept = 0;

            virtual device::error*          ReadTexture             ( const xgpu::texture&                  Texture
                                                                    , std::vector<std::byte>&               Dest
                                                                    , int&                                   Width
                                                                    , int&                                   Height
                                                                    ) noexcept = 0;

            virtual device::error*          Create                  ( xgpu::timestamps&                     Timestamps
                                                                    , const xgpu::timestamps::setup&        Setup
                                                                    , std::shared_ptr<device_handle>&       SharedDevice
                                                                    ) noexcept = 0;
            virtual device::error*          Create                  ( xgpu::compute&                        Compute
                                                                    , const xgpu::compute::setup&           Setup
                                                                    , std::shared_ptr<device_handle>&       SharedDevice
                                                                    ) noexcept = 0;

            virtual void Destroy(pipeline_instance&& PipelineInstance)  noexcept = 0;
            virtual void Destroy(pipeline&& Pipeline)                   noexcept = 0;
            virtual void Destroy(texture&& Texture)                     noexcept = 0;
            virtual void Destroy(buffer&& Buffer)                       noexcept = 0;
            virtual void Destroy(window&& Window)                       noexcept = 0;
            virtual void Destroy(renderpass&& RenderPass)               noexcept = 0;
            virtual void Destroy(texture_table&& Table)                 noexcept = 0;
            virtual device::error*          Create                  ( xgpu::texture_table&                  Table
                                                                    , const xgpu::texture_table::setup&     Setup
                                                                    , std::shared_ptr<device_handle>&       SharedDevice
                                                                    ) noexcept = 0;
            virtual sampler_id              getSampler              ( const pipeline::sampler& Desc )       noexcept = 0;

            virtual void Shutdown(void)                                 noexcept = 0;
            virtual std::uint64_t getFrameIndex(void) const             noexcept = 0;
            virtual bool          isBindless(void) const                noexcept = 0;
            virtual bool          hasDrawIndirectCount(void) const      noexcept = 0;
            virtual timeline_point FlushUploads(void)                   noexcept = 0;
            virtual bool          isDone(timeline_point P)              noexcept = 0;
            virtual void          Wait(timeline_point P)                noexcept = 0;
            virtual std::uint64_t getMainQueueWaits(void) const         noexcept = 0;
            virtual device::memory_stats getMemoryStats(void)           noexcept = 0;
        };
    }

    //------------------------------------------------------------------------------------------------

    XGPU_INLINE
    void device::getInstance
    ( xgpu::instance& Instance
    ) const noexcept
    {
        return m_Private->getInstance( Instance );
    }

    //------------------------------------------------------------------------------------------------

    [[nodiscard]] XGPU_INLINE
    device::error* 
    device::Create
    ( window&               Window
    , const window::setup&  Setup 
    ) noexcept
    {
        return m_Private->Create(Window, Setup, m_Private);
    }

    //------------------------------------------------------------------------------------------------

    [[nodiscard]] XGPU_INLINE 
    device::error* 
    device::Create
    ( renderpass&               Renderpass
    , const renderpass::setup&  Setup
    ) noexcept
    {
        return m_Private->Create(Renderpass, Setup, m_Private);
    }

    //------------------------------------------------------------------------------------------------

    [[nodiscard]] XGPU_INLINE
    device::error* 
    device::Create
    ( pipeline&                   Pipeline
    , const pipeline::setup&      Setup
    ) noexcept
    {
        return m_Private->Create( Pipeline, Setup, m_Private);
    }

    //------------------------------------------------------------------------------------------------

    [[nodiscard]] XGPU_INLINE 
    device::error*
    device::Create
    ( shader&               Shader
    , const shader::setup&  Setup
    ) noexcept
    {
        return m_Private->Create( Shader, Setup, m_Private);
    }

    //------------------------------------------------------------------------------------------------

    [[nodiscard]] XGPU_INLINE 
    device::error* 
    device::Create
    ( vertex_descriptor&                VDescriptor
    , const vertex_descriptor::setup&   Setup 
    ) noexcept
    {
        return m_Private->Create(VDescriptor, Setup, m_Private);
    }

    //------------------------------------------------------------------------------------------------

    [[nodiscard]] XGPU_INLINE 
    device::error* 
    device::Create
    ( texture&                          Texture
    , const texture::setup&             Setup
    ) noexcept
    {
        return m_Private->Create(Texture, Setup, m_Private);
    }

    //------------------------------------------------------------------------------------------------

    [[nodiscard]] XGPU_INLINE 
    device::error* 
    device::Create
    ( pipeline_instance&                PipelineInstance
    , const pipeline_instance::setup&   Setup
    ) noexcept
    {
        return m_Private->Create(PipelineInstance, Setup, m_Private);
    }

    //------------------------------------------------------------------------------------------------

    [[nodiscard]] device::error*
    device::Create
    ( buffer&                   Buffer
    , const buffer::setup&      Setup
    ) noexcept
    {
        return m_Private->Create( Buffer, Setup, m_Private );
    }

    //------------------------------------------------------------------------------------------------

    [[nodiscard]] XGPU_INLINE
    device::error*
    device::UpdateTexture
    ( texture&                      Texture
    , int                           OffsetX
    , int                           OffsetY
    , int                           Width
    , int                           Height
    , std::span<const std::byte>   Source
    ) noexcept
    {
        return m_Private->UpdateTexture(Texture, OffsetX, OffsetY, Width, Height, Source);
    }

    //------------------------------------------------------------------------------------------------

    [[nodiscard]] XGPU_INLINE
    device::error*
    device::ReadTexture
    ( const texture&                Texture
    , std::vector<std::uint32_t>&  Dest
    , int&                          Width
    , int&                          Height
    ) noexcept
    {
        return m_Private->ReadTexture(Texture, Dest, Width, Height);
    }

    //------------------------------------------------------------------------------------------------

    [[nodiscard]] XGPU_INLINE
    device::error*
    device::ReadTexture
    ( const texture&                Texture
    , std::vector<std::byte>&      Dest
    , int&                          Width
    , int&                          Height
    ) noexcept
    {
        return m_Private->ReadTexture(Texture, Dest, Width, Height);
    }

    //------------------------------------------------------------------------------------------------

    [[nodiscard]] XGPU_INLINE
    device::error*
    device::Create
    ( timestamps&                   Timestamps
    , const timestamps::setup&      Setup
    ) noexcept
    {
        return m_Private->Create( Timestamps, Setup, m_Private );
    }

    //------------------------------------------------------------------------------------------------

    [[nodiscard]] XGPU_INLINE
    device::error*
    device::Create
    ( compute&                      Compute
    , const compute::setup&         Setup
    ) noexcept
    {
        return m_Private->Create( Compute, Setup, m_Private );
    }

    //------------------------------------------------------------------------------------------------

    void device::Destroy(pipeline_instance&& PipelineInstance)  noexcept 
    { 
        m_Private->Destroy( std::move(PipelineInstance) );
        PipelineInstance.m_Private.reset();         // (the handles have no move: the queue took a copy - the caller's reference goes too, or its m_Private still says "alive")
    }
    void device::Destroy(pipeline&& Pipeline)                   noexcept 
    {
        m_Private->Destroy( std::move(Pipeline) );
        Pipeline.m_Private.reset();
    }
    void device::Destroy(texture&& Texture)                     noexcept 
    {
        m_Private->Destroy( std::move(Texture) );
        Texture.m_Private.reset();
    }
    void device::Destroy(buffer&& Buffer)                       noexcept
    {
        m_Private->Destroy( std::move(Buffer) );
        Buffer.m_Private.reset();
    }
    void device::Destroy(window&& Window)                       noexcept
    {
        m_Private->Destroy( std::move(Window) );
        Window.m_Private.reset();
    }
    void device::Destroy(texture_table&& Table)                 noexcept
    {
        m_Private->Destroy( std::move(Table) );
        Table.m_Private.reset();
    }
    [[nodiscard]] XGPU_INLINE
    device::error* device::Create( texture_table& Table, const texture_table::setup& Setup ) noexcept
    {
        return m_Private->Create( Table, Setup, m_Private );
    }
    [[nodiscard]] XGPU_INLINE
    sampler_id device::getSampler( const pipeline::sampler& Desc ) noexcept
    {
        return m_Private->getSampler(Desc);
    }
    void device::Destroy(renderpass&& Renderpass)               noexcept
    {
        m_Private->Destroy( std::move(Renderpass) );
        Renderpass.m_Private.reset();
    }

    void device::Shutdown( void ) noexcept
    {
        m_Private->Shutdown();
    }

    //------------------------------------------------------------------------------------------------

    XGPU_INLINE
    std::uint64_t device::getFrameIndex( void ) const noexcept
    {
        return m_Private->getFrameIndex();
    }

    //------------------------------------------------------------------------------------------------

    XGPU_INLINE
    bool device::isBindless( void ) const noexcept
    {
        return m_Private->isBindless();
    }

    XGPU_INLINE
    bool device::hasDrawIndirectCount( void ) const noexcept
    {
        return m_Private->hasDrawIndirectCount();
    }

    XGPU_INLINE timeline_point device::FlushUploads( void ) noexcept               { return m_Private->FlushUploads(); }
    XGPU_INLINE bool           device::isDone( timeline_point P ) noexcept         { return m_Private->isDone(P); }
    XGPU_INLINE void           device::Wait( timeline_point P ) noexcept           { m_Private->Wait(P); }
    XGPU_INLINE std::uint64_t  device::getMainQueueWaits( void ) const noexcept    { return m_Private->getMainQueueWaits(); }
    XGPU_INLINE device::memory_stats device::getMemoryStats( void ) noexcept        { return m_Private->getMemoryStats(); }

}
