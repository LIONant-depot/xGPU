#include <unordered_map>
#include <unordered_set>
std::atomic<std::uint64_t> g_ShaderModules{ 0 }, g_ShaderBytes{ 0 };      // (shader modules made, ever: XGPU_LEAK_LOG)
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

                // (the buffer sized by the file: it was allocated from codeSize before codeSize was set - 0 bytes - and the file read past it)
                const auto Size = static_cast<std::size_t>(File.tellg());
                if( Size == 0 || Size % sizeof(std::uint32_t) )
                {
                    m_Device->m_Instance->ReportError( std::format( "Shader {} is not SPIR-V ({} bytes, not a whole number of 32-bit words)", Type.m_pValue, Size ) );
                    return VGPU_ERROR(xgpu::device::error::FAILURE, "Shader file is not SPIR-V");
                }
                ShaderFile                  = std::make_unique<char[]>(Size);
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

        m_BytecodeHash = HashBytes( 14695981039346656037ull, ShaderCreateInfo.pCode, ShaderCreateInfo.codeSize );

        // Which of set 0's storage buffers the shader only reads: a variable decorated NonWritable, or one whose block's members all are
        // (glslang's readonly buffer) - so the frame map, and the render graph's audit, tell reads from writes
        {
            const auto* W = ShaderCreateInfo.pCode;
            const std::size_t nW = ShaderCreateInfo.codeSize / 4;
            std::unordered_map<std::uint32_t, std::uint32_t> Binding, Set, PtrType, VarPtr, VarClass, StructMembers, StructNW;
            std::unordered_set<std::uint32_t> VarNW;
            for( std::size_t i = 5; i < nW; )
            {
                const std::uint32_t n = W[i] >> 16, Op = W[i] & 0xFFFFu;
                if( n == 0 || i + n > nW ) break;
                if( Op == 71 && n >= 3 )        // OpDecorate target decoration [operand]
                {
                    if( W[i + 2] == 33 && n >= 4 ) Binding[W[i + 1]] = W[i + 3];
                    else if( W[i + 2] == 34 && n >= 4 ) Set[W[i + 1]] = W[i + 3];
                    else if( W[i + 2] == 24 ) VarNW.insert(W[i + 1]);
                }
                else if( Op == 72 && n >= 4 && W[i + 3] == 24 ) ++StructNW[W[i + 1]];       // OpMemberDecorate struct member NonWritable
                else if( Op == 30 ) StructMembers[W[i + 1]] = n - 2;                         // OpTypeStruct id members...
                else if( Op == 32 && n >= 4 ) PtrType[W[i + 1]] = W[i + 3];                  // OpTypePointer id class type
                else if( Op == 59 && n >= 4 ) { VarPtr[W[i + 2]] = W[i + 1]; VarClass[W[i + 2]] = W[i + 3]; }     // OpVariable type id class
                i += n;
            }
            for( const auto& [Var, Ptr] : VarPtr )
            {
                const auto B = Binding.find(Var); if( B == Binding.end() || B->second >= 32 ) continue;
                const auto S = Set.find(Var);
                const std::uint32_t SetIndex = S == Set.end() ? 0u : S->second;
                if( SetIndex > 1 ) continue;
                bool bRO = VarNW.count(Var) != 0 || VarClass[Var] == 2u;      // (class Uniform: a uniform block - read only; storage buffers are class StorageBuffer)
                if( !bRO ) if( const auto P = PtrType.find(Ptr); P != PtrType.end() )
                {
                    const auto M = StructMembers.find(P->second), N = StructNW.find(P->second);
                    bRO = M != StructMembers.end() && N != StructNW.end() && M->second > 0 && N->second >= M->second;
                }
                if( SetIndex == 0 ) { if( bRO ) m_ReadOnlyBuffers |= 1u << B->second; }
                else { m_Set1Declared |= 1u << B->second; if( !bRO ) m_Set1Written |= 1u << B->second; }
            }
        }
        if( const char* Entry = Setup.m_EntryPoint; Entry )
        {
            std::size_t n = 0;
            while( Entry[n] ) ++n;
            m_BytecodeHash = HashBytes( m_BytecodeHash, Entry, n );
        }
        m_BytecodeHash = HashMix( m_BytecodeHash, static_cast<std::uint64_t>(Setup.m_Type) );
        if( m_BytecodeHash == 0 ) m_BytecodeHash = 1;

        g_ShaderModules.fetch_add(1); g_ShaderBytes.fetch_add(ShaderCreateInfo.codeSize);      // (XGPU_LEAK_LOG)
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
                { .sType    = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO
                , .stage    = VK_SHADER_STAGE_VERTEX_BIT
                , .module   = m_VKShaderModule
                , .pName    = Setup.m_EntryPoint
                };
            break;
            case xgpu::shader::type::bit::FRAGMENT:
                m_ShaderStageCreateInfo = VkPipelineShaderStageCreateInfo
                { .sType    = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO
                , .stage    = VK_SHADER_STAGE_FRAGMENT_BIT
                , .module   = m_VKShaderModule
                , .pName    = Setup.m_EntryPoint
                };
            break;
            case xgpu::shader::type::bit::GEOMETRY:
                m_ShaderStageCreateInfo = VkPipelineShaderStageCreateInfo
                { .sType    = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO
                , .stage    = VK_SHADER_STAGE_GEOMETRY_BIT
                , .module   = m_VKShaderModule
                , .pName    = Setup.m_EntryPoint
                };
                break;
            case xgpu::shader::type::bit::COMPUTE:
                m_ShaderStageCreateInfo = VkPipelineShaderStageCreateInfo
                { .sType    = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO
                , .stage    = VK_SHADER_STAGE_COMPUTE_BIT
                , .module   = m_VKShaderModule
                , .pName    = Setup.m_EntryPoint
                };
                break;
            default: assert(false);
        }

        return nullptr;
    }




    //------------------------------------------------------------------------------------------------------------------------

    shader::~shader( void ) noexcept
    {
        if( m_Device && m_VKShaderModule ) vkDestroyShaderModule(m_Device->m_VKDevice, m_VKShaderModule, m_Device->m_Instance->m_pVKAllocator);
    }
}