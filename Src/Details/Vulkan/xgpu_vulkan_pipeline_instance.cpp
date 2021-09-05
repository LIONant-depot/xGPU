namespace xgpu::vulkan
{
    //-------------------------------------------------------------------------------

    pipeline_instance::~pipeline_instance( void ) noexcept
    {
    }

    //-------------------------------------------------------------------------------

    xgpu::device::error* pipeline_instance::Initialize
    ( std::shared_ptr<vulkan::device>&&     Device
    , const xgpu::pipeline_instance::setup& Setup
    ) noexcept
    {
        m_Device   = Device;
        m_Pipeline = std::reinterpret_pointer_cast<vulkan::pipeline>(Setup.m_PipeLine.m_Private);

        if( Setup.m_SamplersBindings.size() != m_Pipeline->m_nSamplers )
        {
            m_Device->m_Instance->ReportError( "You did not give the expected number of textures bindings as required by the pipeline");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "You did not give the expected number of textures bindings as required by the pipeline");
        }

        //
        // Create all the samplers
        //
        for(int i=0; i< m_Pipeline->m_nSamplers; ++i )
        {
            auto& TextureBinds = m_TexturesBinds[i];
            TextureBinds = std::reinterpret_pointer_cast<vulkan::texture>( Setup.m_SamplersBindings[i].m_Texture.m_Private );
        }

        return nullptr;
    }

    //-------------------------------------------------------------------------------

}