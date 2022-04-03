namespace xgpu::vulkan
{
    //----------------------------------------------------------------------------------------
    constexpr static
    auto ConvertSampler
    ( const xgpu::pipeline::sampler::mipmap_sampler MipmapSampler
    ) noexcept
    {
        return (MipmapSampler == xgpu::pipeline::sampler::mipmap_sampler::LINEAR)
                ? VK_FILTER_LINEAR
                : VK_FILTER_NEAREST;
    }
    //----------------------------------------------------------------------------------------
    constexpr static
    auto ConvertMipmapMode
    ( const xgpu::pipeline::sampler::mipmap_mode MipmapMode
    ) noexcept
    {
        return (MipmapMode == xgpu::pipeline::sampler::mipmap_mode::LINEAR)
               ? VK_SAMPLER_MIPMAP_MODE_LINEAR
               : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }

    //----------------------------------------------------------------------------------------

    constexpr static
    auto ConvertAddressMode
    ( const xgpu::pipeline::sampler::address_mode AddressMode
    ) noexcept
    {
        using address_mode = xgpu::pipeline::sampler::address_mode;
        switch (AddressMode)
        {
        case address_mode::TILE:         return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case address_mode::CLAMP:        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case address_mode::CLAMP_BORDER: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case address_mode::MIRROR:       return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case address_mode::MIRROR_CLAMP: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
        default: assert(false);
        }
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }

    //----------------------------------------------------------------------------------------

    xgpu::device::error* pipeline::Initialize
    ( std::shared_ptr<device>&&     Device
    , const xgpu::pipeline::setup&  Setup 
    ) noexcept
    {
        m_Device = Device;

        m_VertexDesciptor = std::reinterpret_pointer_cast<vulkan::vertex_descriptor>(Setup.m_VertexDescriptor.m_Private);

        //
        // Collect some basic information from the uniform buffers
        //
        m_nUniformBuffers = (int)Setup.m_UniformBufferUsage.size();
        for( auto& E : m_UniformBufferFastRemap ) E = 0xff;
        for( int i = 0; i<m_nUniformBuffers; ++i)
        {
            // copy over the bits
            m_UniformBufferBits[i] = Setup.m_UniformBufferUsage[i];

            const std::uint8_t Value = static_cast<std::uint8_t>(Setup.m_UniformBufferUsage[i].m_Value);

            if( Value == 0 )
            {
                m_Device->m_Instance->ReportError("Fail to create a pipeline due to having a uniform buffer without any shader usage");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create a pipeline due to having a uniform buffer without any shader usage");
            }

            for( int j=0; j<xgpu::shader::type::count_v; ++j)
            {
                const int bit  = (Value>>i)&1;

                if(bit)
                {
                    if( m_UniformBufferFastRemap[bit] != 0xff )
                    {
                        m_Device->m_Instance->ReportError("Fail to create a pipeline due to having multiple uniform buffer trying to use the same shader bit");
                        return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create a pipeline due to having multiple uniform buffer trying to use the same shader bit");
                    }

                    // Set the actual remap
                    m_UniformBufferFastRemap[bit] = i;

                    // do we have any other bit to set? if not then just break
                    if( const std::uint8_t Mask = ~((1 << (j + 1)) - 1); (Value & Mask ) == 0 ) 
                    {
                        break;
                    }
                }
            }
        }

        //
        // Rasterization state
        //
        static constexpr auto PolygonModeTable = []() consteval
        {
            std::array< VkPolygonMode, (int)xgpu::pipeline::primitive::raster::ENUM_COUNT > PolygonModeTable {};
            PolygonModeTable[(int)xgpu::pipeline::primitive::raster::FILL]      = VK_POLYGON_MODE_FILL;
            PolygonModeTable[(int)xgpu::pipeline::primitive::raster::WIRELINE]  = VK_POLYGON_MODE_LINE;
            PolygonModeTable[(int)xgpu::pipeline::primitive::raster::POINT]     = VK_POLYGON_MODE_POINT;
            return PolygonModeTable;
        }();

        static constexpr auto CullModeFlagsBitsTable = []() consteval
        {
            std::array< VkCullModeFlagBits, (int)xgpu::pipeline::primitive::cull::ENUM_COUNT> CullModeFlagsBitsTable {};
            CullModeFlagsBitsTable[(int)xgpu::pipeline::primitive::cull::BACK]  = VK_CULL_MODE_BACK_BIT;
            CullModeFlagsBitsTable[(int)xgpu::pipeline::primitive::cull::FRONT] = VK_CULL_MODE_FRONT_BIT;
            CullModeFlagsBitsTable[(int)xgpu::pipeline::primitive::cull::ALL]   = VK_CULL_MODE_FRONT_AND_BACK;
            CullModeFlagsBitsTable[(int)xgpu::pipeline::primitive::cull::NONE]  = VK_CULL_MODE_NONE;
            return CullModeFlagsBitsTable;
        }();

        static constexpr auto FrontFaceTable = []() consteval
        {
            std::array< VkFrontFace, (int)xgpu::pipeline::primitive::front_face::ENUM_COUNT> FrontFaceTable {};
            FrontFaceTable[(int)xgpu::pipeline::primitive::front_face::CLOCKWISE]          = VK_FRONT_FACE_CLOCKWISE;
            FrontFaceTable[(int)xgpu::pipeline::primitive::front_face::COUNTER_CLOCKWISE]  = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            return FrontFaceTable;
        }();

        m_VkRasterizationState.sType                        = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        m_VkRasterizationState.flags                        = 0;
        m_VkRasterizationState.pNext                        = nullptr;
        m_VkRasterizationState.lineWidth                    = Setup.m_Primitive.m_LineWidth;
        m_VkRasterizationState.polygonMode                  = PolygonModeTable[(int)Setup.m_Primitive.m_Raster ];
        m_VkRasterizationState.cullMode                     = CullModeFlagsBitsTable[(int)Setup.m_Primitive.m_Cull ];
        m_VkRasterizationState.frontFace                    = FrontFaceTable[(int)Setup.m_Primitive.m_FrontFace];
        m_VkRasterizationState.depthClampEnable             = Setup.m_DepthStencil.m_bDepthClampEnable;
        m_VkRasterizationState.rasterizerDiscardEnable      = Setup.m_Primitive.m_bRasterizerDiscardEnable;
        m_VkRasterizationState.depthBiasEnable              = Setup.m_DepthStencil.m_bDepthBiasEnable;
        m_VkRasterizationState.depthBiasConstantFactor      = Setup.m_DepthStencil.m_DepthBiasConstantFactor;
        m_VkRasterizationState.depthBiasSlopeFactor         = Setup.m_DepthStencil.m_DepthBiasSlopeFactor;
        m_VkRasterizationState.depthBiasClamp               = Setup.m_DepthStencil.m_DepthBiasClamp;

        //
        // VkPipelineInputAssemblyStateCreateInfo
        //      Vertex input state
        //      Describes the topoloy used with this pipeline
        //      This pipeline renders vertex data as triangle lists
        m_VkInputAssemblyState.sType                        = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        m_VkInputAssemblyState.pNext                        = nullptr;
        m_VkInputAssemblyState.flags                        = 0;
        m_VkInputAssemblyState.primitiveRestartEnable       = false;
        m_VkInputAssemblyState.topology                     = static_cast<const xgpu::vulkan::vertex_descriptor*>(Setup.m_VertexDescriptor.m_Private.get())->m_VKTopology;

        //
        // Blend Attachments
        //
        static constexpr auto BlendOperationTable = []() consteval
        {
            std::array< VkBlendOp, (int)xgpu::pipeline::blend::op::ENUM_COUNT > BlendOperationTable {};
            BlendOperationTable[(int)xgpu::pipeline::blend::op::ADD]                   = VK_BLEND_OP_ADD;
            BlendOperationTable[(int)xgpu::pipeline::blend::op::SUBTRACT]              = VK_BLEND_OP_SUBTRACT;
            BlendOperationTable[(int)xgpu::pipeline::blend::op::REVERSE_SUBTRACT]      = VK_BLEND_OP_REVERSE_SUBTRACT;
            BlendOperationTable[(int)xgpu::pipeline::blend::op::MIN]                   = VK_BLEND_OP_MIN;
            BlendOperationTable[(int)xgpu::pipeline::blend::op::MAX]                   = VK_BLEND_OP_MAX;
            return BlendOperationTable;
        }();

        static constexpr auto BlendFactorTable = []() consteval
        {
            std::array< VkBlendFactor, (int)xgpu::pipeline::blend::factor::ENUM_COUNT > BlendFactorTable {};
            BlendFactorTable[(int)xgpu::pipeline::blend::factor::ZERO]                     = VK_BLEND_FACTOR_ZERO;
            BlendFactorTable[(int)xgpu::pipeline::blend::factor::ONE]                      = VK_BLEND_FACTOR_ONE;
            BlendFactorTable[(int)xgpu::pipeline::blend::factor::SRC_COLOR]                = VK_BLEND_FACTOR_SRC_COLOR;
            BlendFactorTable[(int)xgpu::pipeline::blend::factor::ONE_MINUS_SRC_COLOR]      = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            BlendFactorTable[(int)xgpu::pipeline::blend::factor::DST_COLOR]                = VK_BLEND_FACTOR_DST_COLOR;
            BlendFactorTable[(int)xgpu::pipeline::blend::factor::ONE_MINUS_DST_COLOR]      = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
            BlendFactorTable[(int)xgpu::pipeline::blend::factor::SRC_ALPHA]                = VK_BLEND_FACTOR_SRC_ALPHA;
            BlendFactorTable[(int)xgpu::pipeline::blend::factor::ONE_MINUS_SRC_ALPHA]      = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            BlendFactorTable[(int)xgpu::pipeline::blend::factor::DST_ALPHA]                = VK_BLEND_FACTOR_DST_ALPHA;
            BlendFactorTable[(int)xgpu::pipeline::blend::factor::ONE_MINUS_DST_ALPHA]      = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            BlendFactorTable[(int)xgpu::pipeline::blend::factor::CONSTANT_COLOR]           = VK_BLEND_FACTOR_CONSTANT_COLOR;
            BlendFactorTable[(int)xgpu::pipeline::blend::factor::ONE_MINUS_CONSTANT_COLOR] = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
            BlendFactorTable[(int)xgpu::pipeline::blend::factor::CONSTANT_ALPHA]           = VK_BLEND_FACTOR_CONSTANT_ALPHA;
            BlendFactorTable[(int)xgpu::pipeline::blend::factor::ONE_MINUS_CONSTANT_ALPHA] = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
            BlendFactorTable[(int)xgpu::pipeline::blend::factor::SRC_ALPHA_SATURATE]       = VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
            BlendFactorTable[(int)xgpu::pipeline::blend::factor::SRC1_COLOR]               = VK_BLEND_FACTOR_SRC1_COLOR;
            BlendFactorTable[(int)xgpu::pipeline::blend::factor::ONE_MINUS_SRC1_COLOR]     = VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
            BlendFactorTable[(int)xgpu::pipeline::blend::factor::SRC1_ALPHA]               = VK_BLEND_FACTOR_SRC1_ALPHA;
            BlendFactorTable[(int)xgpu::pipeline::blend::factor::ONE_MINUS_SRC1_ALPHA]     = VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
            return BlendFactorTable;
        }();

        // When doing mutipass rendering you may have more than just one destination buffer and we should know how 
        // that to do with each one of them
        for( auto& BA : m_lVkBlendAttachmentState )
        {
            BA.colorWriteMask         = Setup.m_Blend.m_ColorWriteMask;
            BA.blendEnable            = Setup.m_Blend.m_bEnable;
            BA.srcColorBlendFactor    = BlendFactorTable[(int)Setup.m_Blend.m_ColorSrcFactor];
            BA.dstColorBlendFactor    = BlendFactorTable[(int)Setup.m_Blend.m_ColorDstFactor];
            BA.colorBlendOp           = BlendOperationTable[(int)Setup.m_Blend.m_ColorOperation];
            BA.srcAlphaBlendFactor    = BlendFactorTable[(int)Setup.m_Blend.m_AlphaSrcFactor];
            BA.dstAlphaBlendFactor    = BlendFactorTable[(int)Setup.m_Blend.m_AlphaDstFactor];
            BA.alphaBlendOp           = BlendOperationTable[(int)Setup.m_Blend.m_AlphaOperation];
        }

        m_VkColorBlendState.sType                           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        m_VkColorBlendState.attachmentCount                 = static_cast<std::uint32_t>(m_lVkBlendAttachmentState.size());
        m_VkColorBlendState.pAttachments                    = m_lVkBlendAttachmentState.data();

        //
        // VkPipelineMultisampleStateCreateInfo
        //      Multi sampling state
        //      No multi sampling
        //
        m_VkMultisampleState.sType                          = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        m_VkMultisampleState.pNext                          = nullptr;
        m_VkMultisampleState.flags                          = 0;
        m_VkMultisampleState.pSampleMask                    = nullptr;
        m_VkMultisampleState.rasterizationSamples           = VK_SAMPLE_COUNT_1_BIT;
        m_VkMultisampleState.sampleShadingEnable            = false;
        m_VkMultisampleState.minSampleShading               = 0;
        m_VkMultisampleState.alphaToCoverageEnable          = false;
        m_VkMultisampleState.alphaToOneEnable               = false;

        //
        // VkPipelineDepthStencilStateCreateInfo
        //      Depth and stencil state
        //      Describes depth and stenctil test and compare ops
        //      Basic depth compare setup with depth writes and depth test enabled
        //      No stencil used 
        static constexpr auto StencilOperationsTable = []() consteval
        {
            std::array< VkStencilOp, (int)xgpu::pipeline::depth_stencil::stencil_op::ENUM_COUNT > StencilOperationsTable {};
            StencilOperationsTable[(int)xgpu::pipeline::depth_stencil::stencil_op::KEEP]                   =  VK_STENCIL_OP_KEEP;
            StencilOperationsTable[(int)xgpu::pipeline::depth_stencil::stencil_op::ZERO]                   =  VK_STENCIL_OP_ZERO;
            StencilOperationsTable[(int)xgpu::pipeline::depth_stencil::stencil_op::REPLACE]                =  VK_STENCIL_OP_REPLACE;
            StencilOperationsTable[(int)xgpu::pipeline::depth_stencil::stencil_op::INCREMENT_AND_CLAMP]    =  VK_STENCIL_OP_INCREMENT_AND_CLAMP;
            StencilOperationsTable[(int)xgpu::pipeline::depth_stencil::stencil_op::DECREMENT_AND_CLAMP]    =  VK_STENCIL_OP_DECREMENT_AND_CLAMP;
            StencilOperationsTable[(int)xgpu::pipeline::depth_stencil::stencil_op::INVERT]                 =  VK_STENCIL_OP_INVERT;
            StencilOperationsTable[(int)xgpu::pipeline::depth_stencil::stencil_op::INCREMENT_AND_WRAP]     =  VK_STENCIL_OP_INCREMENT_AND_WRAP;
            StencilOperationsTable[(int)xgpu::pipeline::depth_stencil::stencil_op::DECREMENT_AND_WRAP]     =  VK_STENCIL_OP_DECREMENT_AND_WRAP;
            return StencilOperationsTable;
        }();

        static constexpr auto DepthCompareTable = []() consteval
        {
            std::array< VkCompareOp, (int)xgpu::pipeline::depth_stencil::depth_compare::ENUM_COUNT> DepthCompareTable {};
            DepthCompareTable[(int)xgpu::pipeline::depth_stencil::depth_compare::LESS]                 =  VK_COMPARE_OP_LESS;
            DepthCompareTable[(int)xgpu::pipeline::depth_stencil::depth_compare::LESS_OR_EQUAL]        =  VK_COMPARE_OP_LESS_OR_EQUAL;
            DepthCompareTable[(int)xgpu::pipeline::depth_stencil::depth_compare::GREATER]              =  VK_COMPARE_OP_GREATER;
            DepthCompareTable[(int)xgpu::pipeline::depth_stencil::depth_compare::NOT_EQUAL]            =  VK_COMPARE_OP_NOT_EQUAL;
            DepthCompareTable[(int)xgpu::pipeline::depth_stencil::depth_compare::GREATER_OR_EQUAL]     =  VK_COMPARE_OP_GREATER_OR_EQUAL;
            DepthCompareTable[(int)xgpu::pipeline::depth_stencil::depth_compare::EQUAL]                =  VK_COMPARE_OP_EQUAL;
            DepthCompareTable[(int)xgpu::pipeline::depth_stencil::depth_compare::NEVER]                =  VK_COMPARE_OP_NEVER;
            DepthCompareTable[(int)xgpu::pipeline::depth_stencil::depth_compare::ALWAYS]               =  VK_COMPARE_OP_ALWAYS;
            return DepthCompareTable;
        }();

        m_VkDepthStencilState.sType                         = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        m_VkDepthStencilState.pNext                         = nullptr;
        m_VkDepthStencilState.flags                         = 0;
        m_VkDepthStencilState.depthTestEnable               = Setup.m_DepthStencil.m_bDepthTestEnable;
        m_VkDepthStencilState.depthWriteEnable              = Setup.m_DepthStencil.m_bDepthWriteEnable;
        m_VkDepthStencilState.depthCompareOp                = DepthCompareTable[(int)Setup.m_DepthStencil.m_DepthCompare ];
        m_VkDepthStencilState.depthBoundsTestEnable         = Setup.m_DepthStencil.m_bDepthBoundsTestEnable;
        m_VkDepthStencilState.stencilTestEnable             = Setup.m_DepthStencil.m_StencilTestEnable;

        m_VkDepthStencilState.front.compareOp               = DepthCompareTable[(int)Setup.m_DepthStencil.m_StencilFrontFace.m_CompareOp ];
        m_VkDepthStencilState.front.depthFailOp             = StencilOperationsTable[(int)Setup.m_DepthStencil.m_StencilFrontFace.m_DepthFailOp ];
        m_VkDepthStencilState.front.failOp                  = StencilOperationsTable[(int)Setup.m_DepthStencil.m_StencilFrontFace.m_FailOp ];
        m_VkDepthStencilState.front.passOp                  = StencilOperationsTable[(int)Setup.m_DepthStencil.m_StencilFrontFace.m_PassOp ];
        m_VkDepthStencilState.front.reference               = Setup.m_DepthStencil.m_StencilFrontFace.m_Reference;
        m_VkDepthStencilState.front.compareMask             = Setup.m_DepthStencil.m_StencilFrontFace.m_CompareMask;

        m_VkDepthStencilState.back.compareOp                = DepthCompareTable[(int)Setup.m_DepthStencil.m_StencilBackFace.m_CompareOp ];
        m_VkDepthStencilState.back.depthFailOp              = StencilOperationsTable[(int)Setup.m_DepthStencil.m_StencilBackFace.m_DepthFailOp ];
        m_VkDepthStencilState.back.failOp                   = StencilOperationsTable[(int)Setup.m_DepthStencil.m_StencilBackFace.m_FailOp ];
        m_VkDepthStencilState.back.passOp                   = StencilOperationsTable[(int)Setup.m_DepthStencil.m_StencilBackFace.m_PassOp ];
        m_VkDepthStencilState.back.reference                = Setup.m_DepthStencil.m_StencilBackFace.m_Reference;
        m_VkDepthStencilState.back.compareMask              = Setup.m_DepthStencil.m_StencilBackFace.m_CompareMask;

        m_VkDepthStencilState.minDepthBounds                = Setup.m_DepthStencil.m_DepthMinBounds;
        m_VkDepthStencilState.maxDepthBounds                = Setup.m_DepthStencil.m_DepthMaxBounds;

        //
        // Copy some of the setup data
        //
        for( auto& S : Setup.m_Shaders )
        {
            auto F = const_cast<std::remove_const_t<decltype(S->m_Private)>&>(S->m_Private);

            m_ShaderStages[m_nShaderStages]             = std::reinterpret_pointer_cast<xgpu::vulkan::shader>(std::move(F));
            m_ShaderStagesCreateInfo[m_nShaderStages]   = m_ShaderStages[m_nShaderStages]->m_ShaderStageCreateInfo;
            m_nShaderStages++;
        }

        // In Vulkan textures are accessed by samplers
        // This separates all the sampling information from the 
        // texture data
        // This means you could have multiple sampler objects
        // for the same texture with different settings
        // Similar to the samplers available with OpenGL 3.3
        // Here we just backup the information for our sampler since
        // Untill we don't create the instance of the pipeline we don't
        // have enough information of create the actual sampler
        for( auto& S : Setup.m_Samplers )
        {
            VkSamplerCreateInfo SamplerCreateInfo
            {
                .sType              = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO
            ,   .pNext              = nullptr
            ,   .magFilter          = ConvertSampler(S.m_MipmapMag)
            ,   .minFilter          = ConvertSampler(S.m_MipmapMin)
            ,   .mipmapMode         = ConvertMipmapMode(S.m_MipmapMode)
            ,   .addressModeU       = ConvertAddressMode(S.m_AddressMode[0])
            ,   .addressModeV       = ConvertAddressMode(S.m_AddressMode[1])
            ,   .addressModeW       = ConvertAddressMode(S.m_AddressMode[2])
            ,   .mipLodBias         = 0.0f
            ,   .anisotropyEnable   = !S.m_bDisableAnisotropy
            ,   .maxAnisotropy      = S.m_MaxAnisotropy
            ,   .compareOp          = VK_COMPARE_OP_NEVER
            ,   .minLod             = 0.0f
            ,   .maxLod             = VK_LOD_CLAMP_NONE
            ,   .borderColor        = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
            };

            if( auto VKErr = vkCreateSampler( m_Device->m_VKDevice
                                            , &SamplerCreateInfo
                                            , m_Device->m_Instance->m_pVKAllocator
                                            , &m_VKSamplers[m_nSamplers++]
                                            ); VKErr )
            {
                m_Device->m_Instance->ReportError( VKErr, "Fail to create a texture Sampler" );
                return VGPU_ERROR( xgpu::device::error::FAILURE, "Fail to create a texture Sampler" );
            }
        }

        //
        // Set the descriptors
        //
        {
            // Add all the set zero bindings 
            if(m_nSamplers)
            {
                std::array<VkDescriptorSetLayoutBinding, 16>    layoutBinding = {};
                for( int i = 0; i < m_nSamplers; ++i)
                {
                    layoutBinding[i].binding            = i;
                    layoutBinding[i].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    layoutBinding[i].descriptorCount    = 1;
                    layoutBinding[i].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
                    layoutBinding[i].pImmutableSamplers = &m_VKSamplers[i];
                }

                VkDescriptorSetLayoutCreateInfo DescriptorLayout = {};
                DescriptorLayout.sType          = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                DescriptorLayout.pNext          = nullptr;
                DescriptorLayout.bindingCount   = static_cast<uint32_t>(m_nSamplers);
                DescriptorLayout.pBindings      = layoutBinding.data();

                if (auto VKErr = vkCreateDescriptorSetLayout(m_Device->m_VKDevice, &DescriptorLayout, m_Device->m_Instance->m_pVKAllocator, &m_VKDescriptorSetLayout[m_nVKDescriptorSetLayout++]); VKErr)
                {
                    m_Device->m_Instance->ReportError(VKErr, "Fail to create the samplers Descriptor Set Layout in the pipeline");
                    return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create the samplers Descriptor Set Layout in the pipeline");
                }
            }

            // Add all the set one bindings
            if(m_nUniformBuffers)
            {
                auto ConvertFlagsToVulkan = [&](int Index) noexcept
                {
                    VkShaderStageFlags  V       = 0;
                    const auto&         UBBit   = m_UniformBufferBits[Index];
                    if (UBBit.m_bVertex)                   V |= VK_SHADER_STAGE_VERTEX_BIT;
                    if (UBBit.m_bCompute)                  V |= VK_SHADER_STAGE_COMPUTE_BIT;
                    if (UBBit.m_bFragment)                 V |= VK_SHADER_STAGE_FRAGMENT_BIT;
                    if (UBBit.m_bGeometry)                 V |= VK_SHADER_STAGE_GEOMETRY_BIT;
                    if (UBBit.m_bTessellationControl)      V |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
                    if (UBBit.m_bTessellationEvaluator)    V |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
                    return V;
                };

                std::array<VkDescriptorSetLayoutBinding, 4>    layoutBinding = {};
                for (int i = 0; i < m_nUniformBuffers; ++i)
                {
                    layoutBinding[i].binding            = i;
                    layoutBinding[i].descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                    layoutBinding[i].descriptorCount    = 1;
                    layoutBinding[i].stageFlags         = ConvertFlagsToVulkan(i);
                    layoutBinding[i].pImmutableSamplers = nullptr;
                }

                VkDescriptorSetLayoutCreateInfo DescriptorLayout = {};
                DescriptorLayout.sType          = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                DescriptorLayout.pNext          = nullptr;
                DescriptorLayout.bindingCount   = static_cast<uint32_t>(m_nUniformBuffers);
                DescriptorLayout.pBindings      = layoutBinding.data();

                if (auto VKErr = vkCreateDescriptorSetLayout(m_Device->m_VKDevice, &DescriptorLayout, m_Device->m_Instance->m_pVKAllocator, &m_VKDescriptorSetLayout[m_nVKDescriptorSetLayout++]); VKErr)
                {
                    m_Device->m_Instance->ReportError(VKErr, "Fail to create a Descriptor Set Layout in the pipeline");
                    return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create a Descriptor Set Layout in the pipeline");
                }
            }
        }

        //
        // Set the push constants for this pipeline
        //
        {
            std::array<VkPushConstantRange, 1> pushConstantRange;

            if(Setup.m_PushConstantsSize)
            {
                pushConstantRange[0].offset     = 0;
                pushConstantRange[0].size       = static_cast<std::uint32_t>(Setup.m_PushConstantsSize);
                pushConstantRange[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;
            }

            //
            // Set the pipeline descriptor 
            //
            auto PipelineLayoutCreateInfo = VkPipelineLayoutCreateInfo
            {
                .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
            ,   .pNext                  = nullptr

                // Set the descriptors
            ,   .setLayoutCount         = static_cast<uint32_t>(m_nVKDescriptorSetLayout)
            ,   .pSetLayouts            = m_VKDescriptorSetLayout.data()

                // Push constant ranges are part of the pipeline layout
            ,   .pushConstantRangeCount = Setup.m_PushConstantsSize ? 1u : 0u
            ,   .pPushConstantRanges    = pushConstantRange.data()
            };

            if( auto VKErr = vkCreatePipelineLayout(m_Device->m_VKDevice, &PipelineLayoutCreateInfo, m_Device->m_Instance->m_pVKAllocator, &m_VKPipelineLayout); VKErr )
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to create a Pipeline Layout");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create a Pipeline Layout");
            }
        }

        //
        // Viewport & Clip (Dynamic states)
        //
        m_VKViewportInfo = VkPipelineViewportStateCreateInfo
        { .sType            = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
        , .viewportCount    = 1
        , .scissorCount     = 1
        };

        m_VKDyanmicStates[0] = VK_DYNAMIC_STATE_VIEWPORT;
        m_VKDyanmicStates[1] = VK_DYNAMIC_STATE_SCISSOR;
        m_VKDynamicStateCreateInfo = VkPipelineDynamicStateCreateInfo
        { .sType                = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
        , .dynamicStateCount    = static_cast<std::uint32_t>(m_VKDyanmicStates.size())
        , .pDynamicStates       = m_VKDyanmicStates.data()
        };

        //
        //  VkGraphicsPipelineCreateInfo
        //
        m_VkPipelineCreateInfo.sType                        = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        m_VkPipelineCreateInfo.layout                       = m_VKPipelineLayout;
        m_VkPipelineCreateInfo.pVertexInputState            = &static_cast<const xgpu::vulkan::vertex_descriptor*>(Setup.m_VertexDescriptor.m_Private.get())->m_VKInputStageCreateInfo;
        m_VkPipelineCreateInfo.pInputAssemblyState          = &m_VkInputAssemblyState;
        m_VkPipelineCreateInfo.pRasterizationState          = &m_VkRasterizationState;
        m_VkPipelineCreateInfo.pColorBlendState             = &m_VkColorBlendState;
        m_VkPipelineCreateInfo.pMultisampleState            = &m_VkMultisampleState;
        m_VkPipelineCreateInfo.pDepthStencilState           = &m_VkDepthStencilState;
        m_VkPipelineCreateInfo.stageCount                   = static_cast<std::uint32_t>(m_nShaderStages);
        m_VkPipelineCreateInfo.pStages                      = m_ShaderStagesCreateInfo.data();
        m_VkPipelineCreateInfo.pViewportState               = &m_VKViewportInfo;
        m_VkPipelineCreateInfo.pDynamicState                = &m_VKDynamicStateCreateInfo;

        //
        // All these will be set at the instance of creation
        //
        m_VkPipelineCreateInfo.renderPass       = 0;

        return nullptr;
    }
}