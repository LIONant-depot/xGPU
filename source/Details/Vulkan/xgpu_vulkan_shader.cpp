namespace xgpu::vulkan
{
    //-------------------------------------------------------------------------

    xgpu::device::error* shader::Initialize
    ( std::shared_ptr<vulkan::device>&& Device
    , const xgpu::shader::setup&        Setup 
    ) noexcept
    {
        m_Device = Device;

        VkShaderModuleCreateInfo ShaderCreateInfo
        {
            .sType      = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
        };

        std::unique_ptr<char[]> ShaderFile {};
        if( auto Err = std::visit( [&]( auto& Type ) ->xgpu::device::error*
        {
            if constexpr( std::is_same_v< std::decay_t<decltype(Type)>, xgpu::shader::setup::file_name > )
            {
                std::ifstream File( Type.m_pValue, std::ios::ate | std::ios::binary );
                if( !File.is_open() )
                {
                    m_Device->m_Instance->ReportError( std::format( "Failed to load shader {}", Type.m_pValue ) );
                    return VGPU_ERROR(xgpu::device::error::FAILURE, "Failed to load shader");
                }

                const auto Size = static_cast<std::size_t>(File.tellg());
                ShaderFile                  = std::make_unique<char[]>(ShaderCreateInfo.codeSize);
                ShaderCreateInfo.pCode      = reinterpret_cast<std::uint32_t*>(ShaderFile.get());
                ShaderCreateInfo.codeSize   = Size;

                File.seekg(0);
                File.read( ShaderFile.get(), Size );
                File.close();
            }
            else
            {
                ShaderCreateInfo.codeSize   = static_cast<std::uint32_t>(Type.m_Value.size() * sizeof(decltype(*Type.m_Value.data())));
                ShaderCreateInfo.pCode      = reinterpret_cast<const uint32_t*>(Type.m_Value.data());
            }

            return nullptr;

        }, Setup.m_Sharer ); Err ) return Err;

        if( auto VKErr = vkCreateShaderModule( m_Device->m_VKDevice
                                             , &ShaderCreateInfo
                                             , m_Device->m_Instance->m_pVKAllocator
                                             , &m_VKShaderModule ); VKErr )
        {
            m_Device->m_Instance->ReportError( VKErr, "Failed to create shader module" );
            return VGPU_ERROR( xgpu::device::error::FAILURE, "Failed to create shader module" );
        }

        switch (Setup.m_Type)
        {
            case xgpu::shader::type::bit::VERTEX:
                m_ShaderStageCreateInfo = VkPipelineShaderStageCreateInfo
                {
                  .sType    = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO
                , .stage    = VK_SHADER_STAGE_VERTEX_BIT
                , .module   = m_VKShaderModule
                , .pName    = Setup.m_EntryPoint
                };
            break;
            case xgpu::shader::type::bit::FRAGMENT:
                m_ShaderStageCreateInfo = VkPipelineShaderStageCreateInfo
                {
                  .sType    = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO
                , .stage    = VK_SHADER_STAGE_FRAGMENT_BIT
                , .module   = m_VKShaderModule
                , .pName    = Setup.m_EntryPoint
                };
            break;
            default:
                // TODO: Implement these too
                assert(false);
        }

        return nullptr;
    }



}