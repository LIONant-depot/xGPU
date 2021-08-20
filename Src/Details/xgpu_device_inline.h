namespace xgpu
{
    namespace details
    {
        struct device_handle
        {
            virtual                        ~device_handle           ( void ) noexcept = default;
            virtual device::error*          Create                  ( xgpu::window&                         Window
                                                                    , const xgpu::window::setup&            Setup
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
        };
    }

    //------------------------------------------------------------------------------------------------

    VGPU_INLINE
    [[nodiscard]] device::error* 
    device::Create
    ( window&               Window
    , const window::setup&  Setup 
    ) noexcept
    {
        return m_Private->Create(Window, Setup, m_Private);
    }

    //------------------------------------------------------------------------------------------------

    VGPU_INLINE
    [[nodiscard]] device::error* 
    device::Create
    (
        pipeline&                   Pipeline
    ,   const pipeline::setup&      Setup
    ) noexcept
    {
        return m_Private->Create( Pipeline, Setup, m_Private);
    }

    //------------------------------------------------------------------------------------------------

    VGPU_INLINE 
    [[nodiscard]] device::error*
    device::Create
    ( shader&               Shader
    , const shader::setup&  Setup
    ) noexcept
    {
        return m_Private->Create( Shader, Setup, m_Private);
    }

    //------------------------------------------------------------------------------------------------

    VGPU_INLINE 
    [[nodiscard]] device::error* 
    device::Create
    ( vertex_descriptor&                VDescriptor
    , const vertex_descriptor::setup&   Setup 
    ) noexcept
    {
        return m_Private->Create(VDescriptor, Setup, m_Private);
    }

    //------------------------------------------------------------------------------------------------

    VGPU_INLINE 
    [[nodiscard]] device::error* 
    device::Create
    ( texture&                          Texture
    , const texture::setup&             Setup
    ) noexcept
    {
        return m_Private->Create(Texture, Setup, m_Private);
    }

    //------------------------------------------------------------------------------------------------

    VGPU_INLINE 
    [[nodiscard]] device::error* 
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

}
