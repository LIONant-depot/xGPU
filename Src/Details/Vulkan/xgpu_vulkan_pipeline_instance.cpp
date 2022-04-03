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

        if constexpr( true )
        {
            if( Setup.m_SamplersBindings.size() != m_Pipeline->m_nSamplers )
            {
                m_Device->m_Instance->ReportError( "You did not give the expected number of textures bindings as required by the pipeline");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "You did not give the expected number of textures bindings as required by the pipeline");
            }

            if (Setup.m_UniformBuffersBindings.size() != m_Pipeline->m_nUniformBuffers)
            {
                m_Device->m_Instance->ReportError("You did not give the expected number of uniform buffers as required by the pipeline");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "You did not give the expected number of uniform buffers as required by the pipeline");
            }

            for (int i = 0; i < m_Pipeline->m_nUniformBuffers; ++i)
            {
                Setup.m_UniformBuffersBindings[i].m_Value.getEntryCount();

                std::shared_ptr<vulkan::buffer> Pointer = std::reinterpret_pointer_cast<vulkan::buffer>(Setup.m_UniformBuffersBindings[i].m_Value.m_Private);
                if( Pointer->m_Type != xgpu::buffer::type::UNIFORM ) 
                {
                    m_Device->m_Instance->ReportError("Trying to create a pipeline instance with a non-uniform-buffer");
                    return VGPU_ERROR(xgpu::device::error::FAILURE, "Trying to create a pipeline instance with a non-uniform-buffer");
                }
            }
        }

        //
        // Create all the samplers
        //
        for(int i=0; i< m_Pipeline->m_nSamplers; ++i )
        {
            auto& TextureBinds = m_TexturesBinds[i];
            TextureBinds = std::reinterpret_pointer_cast<vulkan::texture>( Setup.m_SamplersBindings[i].m_Value.m_Private );
        }

        //
        // Back up all the Uniform Buffers
        //
        for(int i=0; i<m_Pipeline->m_nUniformBuffers; ++i )
        {
            auto& Uniform = m_UniformBinds[i];
            Uniform = std::reinterpret_pointer_cast<vulkan::buffer>(Setup.m_UniformBuffersBindings[i].m_Value.m_Private);
        }

        return nullptr;
    }

    //-------------------------------------------------------------------------------

    void* pipeline_instance::getUniformBufferVMem(std::uint32_t& DynamicOffset, xgpu::shader::type::bit ShaderType, std::size_t Size) noexcept
    {
        const auto Index = m_Pipeline->m_UniformBufferFastRemap[(int)ShaderType];

        // if this is true then the user did not defined a uniform for this type of shader in the pipeline definition
        assert(Index != 0xff);

        // get the reference to the right uniform buffer entry
        auto& Uniform = *m_UniformBinds[Index];

        // The user must have specified the wrong type to cast 
        assert(Uniform.m_EntrySizeBytes == Size);

        return Uniform.getUniformBufferVMem(DynamicOffset);
    }


}