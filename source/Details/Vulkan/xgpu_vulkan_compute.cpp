namespace xgpu::vulkan
{
    xgpu::device::error* compute::Initialize( std::shared_ptr<device>&& Device, const xgpu::compute::setup& Setup ) noexcept
    {
        m_Device   = std::move(Device);
        m_nBuffers  = Setup.m_nBuffers;
        m_nTextures = Setup.m_nTextures;
        m_PushSize = static_cast<std::uint32_t>(Setup.m_PushConstantsSize);
        if( Setup.m_pShader == nullptr || Setup.m_pShader->m_Private == nullptr ) return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: no shader");
        if( m_nBuffers < 0 || m_nBuffers > max_buffers_v )                        return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: 0 to 16 storage buffers");
        if( m_PushSize > 128 )                                                    return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: more than 128 bytes of push constants");
        if( m_nTextures < 0 || m_nTextures > max_textures_v )                     return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: 0 to 8 textures");
        const auto& Shader = *static_cast<const vulkan::shader*>(Setup.m_pShader->m_Private.get());

        std::array<VkDescriptorSetLayoutBinding, max_buffers_v> Bindings{};
        for( int i = 0; i < m_nBuffers; ++i )
            Bindings[i] = { .binding = static_cast<std::uint32_t>(i), .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT };
        const VkDescriptorSetLayoutCreateInfo LayoutInfo
        { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = static_cast<std::uint32_t>(m_nBuffers), .pBindings = Bindings.data() };
        if( vkCreateDescriptorSetLayout(m_Device->m_VKDevice, &LayoutInfo, m_Device->m_Instance->m_pVKAllocator, &m_VKSetLayout) )
            return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: vkCreateDescriptorSetLayout failed");
        if( m_nTextures )
        {
            std::array<VkDescriptorSetLayoutBinding, max_textures_v> TextureBindings{};
            for( int i = 0; i < m_nTextures; ++i )
                TextureBindings[i] = { .binding = static_cast<std::uint32_t>(i), .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT };
            const VkDescriptorSetLayoutCreateInfo TextureInfo
            { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = static_cast<std::uint32_t>(m_nTextures), .pBindings = TextureBindings.data() };
            if( vkCreateDescriptorSetLayout(m_Device->m_VKDevice, &TextureInfo, m_Device->m_Instance->m_pVKAllocator, &m_VKTextureLayout) )
                return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: vkCreateDescriptorSetLayout (textures) failed");
            const VkSamplerCreateInfo SamplerInfo
            { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST
            , .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            , .maxLod = VK_LOD_CLAMP_NONE };
            if( vkCreateSampler(m_Device->m_VKDevice, &SamplerInfo, m_Device->m_Instance->m_pVKAllocator, &m_VKSampler) )
                return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: vkCreateSampler failed");
        }

        const VkPushConstantRange Push{ .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = m_PushSize };
        const std::array SetLayouts{ m_VKSetLayout, m_VKTextureLayout };
        const VkPipelineLayoutCreateInfo PipelineLayoutInfo
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = m_nTextures ? 2u : 1u, .pSetLayouts = SetLayouts.data()
        , .pushConstantRangeCount = m_PushSize ? 1u : 0u, .pPushConstantRanges = m_PushSize ? &Push : nullptr };
        if( vkCreatePipelineLayout(m_Device->m_VKDevice, &PipelineLayoutInfo, m_Device->m_Instance->m_pVKAllocator, &m_VKPipelineLayout) )
            return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: vkCreatePipelineLayout failed");

        VkPipelineShaderStageCreateInfo Stage = Shader.m_ShaderStageCreateInfo;
        if( Stage.stage != VK_SHADER_STAGE_COMPUTE_BIT ) return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: the shader is not a compute shader");
        const VkComputePipelineCreateInfo PipelineInfo{ .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = Stage, .layout = m_VKPipelineLayout };
        if( vkCreateComputePipelines(m_Device->m_VKDevice, VK_NULL_HANDLE, 1, &PipelineInfo, m_Device->m_Instance->m_pVKAllocator, &m_VKPipeline) )
            return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: vkCreateComputePipelines failed");
        return nullptr;
    }

    compute::~compute( void ) noexcept
    {
        if( !m_Device ) return;
        vkDeviceWaitIdle(m_Device->m_VKDevice);         // ponytail: a compute object lives as long as its renderer; wait instead of a death march
        if( m_VKPipeline )       vkDestroyPipeline(m_Device->m_VKDevice, m_VKPipeline, m_Device->m_Instance->m_pVKAllocator);
        if( m_VKPipelineLayout ) vkDestroyPipelineLayout(m_Device->m_VKDevice, m_VKPipelineLayout, m_Device->m_Instance->m_pVKAllocator);
        if( m_VKSetLayout )      vkDestroyDescriptorSetLayout(m_Device->m_VKDevice, m_VKSetLayout, m_Device->m_Instance->m_pVKAllocator);
        if( m_VKTextureLayout )  vkDestroyDescriptorSetLayout(m_Device->m_VKDevice, m_VKTextureLayout, m_Device->m_Instance->m_pVKAllocator);
        if( m_VKSampler )        vkDestroySampler(m_Device->m_VKDevice, m_VKSampler, m_Device->m_Instance->m_pVKAllocator);
    }

    //----------------------------------------------------------------------------------------------------------

    void window::Dispatch( xgpu::compute& Compute, std::span<xgpu::buffer* const> Buffers, std::span<const xgpu::texture* const> Textures, const void* pPush, std::size_t PushSize, int X, int Y, int Z ) noexcept
    {
        auto& C = *static_cast<vulkan::compute*>(Compute.m_Private.get());
        assert(static_cast<int>(Buffers.size()) == C.m_nBuffers && static_cast<int>(Textures.size()) == C.m_nTextures && PushSize <= C.m_PushSize);
        auto& Frame = m_Frames[m_FrameIndex];

        VkDescriptorSet Set = VK_NULL_HANDLE;
        if( C.m_nBuffers )
        {
            if( (Set = Rings()[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER].alloc(m_Device->m_VKDevice, C.m_VKSetLayout)) == VK_NULL_HANDLE )
            {
                if( !m_bReportedSetOverflow ) { m_bReportedSetOverflow = true; m_Device->m_Instance->ReportError("xGPU: a frame ran out of descriptor sets (compute); the dispatch is skipped"); }
                return;
            }
            std::array<VkDescriptorBufferInfo, compute::max_buffers_v> Infos{};
            std::array<VkWriteDescriptorSet, compute::max_buffers_v>   Writes{};
            for( int i = 0; i < C.m_nBuffers; ++i )
            {
                Infos[i]  = { .buffer = static_cast<vulkan::buffer*>(Buffers[i]->m_Private.get())->m_VKBuffer, .offset = 0, .range = VK_WHOLE_SIZE };
                Writes[i] = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = Set, .dstBinding = static_cast<std::uint32_t>(i), .descriptorCount = 1
                            , .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &Infos[i] };
            }
            vkUpdateDescriptorSets(m_Device->m_VKDevice, static_cast<std::uint32_t>(C.m_nBuffers), Writes.data(), 0, nullptr);
        }
        VkDescriptorSet TextureSet = VK_NULL_HANDLE;
        if( C.m_nTextures )
        {
            if( (TextureSet = Rings()[VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER].alloc(m_Device->m_VKDevice, C.m_VKTextureLayout)) == VK_NULL_HANDLE )
            {
                if( !m_bReportedSetOverflow ) { m_bReportedSetOverflow = true; m_Device->m_Instance->ReportError("xGPU: a frame ran out of descriptor sets (compute textures); the dispatch is skipped"); }
                return;
            }
            std::array<VkDescriptorImageInfo, compute::max_textures_v> Infos{};
            std::array<VkWriteDescriptorSet, compute::max_textures_v> Writes{};
            for( int i = 0; i < C.m_nTextures; ++i )
            {
                const auto& T = *static_cast<const vulkan::texture*>(Textures[i]->m_Private.get());
                Infos[i]  = { .sampler = C.m_VKSampler, .imageView = T.m_VKDescriptorImageInfo.imageView, .imageLayout = T.m_VKDescriptorImageInfo.imageLayout };
                Writes[i] = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = TextureSet, .dstBinding = static_cast<std::uint32_t>(i), .descriptorCount = 1
                            , .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &Infos[i] };
            }
            vkUpdateDescriptorSets(m_Device->m_VKDevice, static_cast<std::uint32_t>(C.m_nTextures), Writes.data(), 0, nullptr);
        }

        // Earlier work (draws, copies, the host's writes, other dispatches) is done with the buffers before this one writes them
        // (DRAW_INDIRECT: a buffer of draw commands it rewrites may still be read by earlier indirect draws, and later ones read what it wrote)
        // (the attachment stages: a texture it reads was rendered by an earlier pass, and a later pass may render into it again)
        constexpr VkPipelineStageFlags Before = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                              | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT
                                              | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        const VkMemoryBarrier In
        { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER
        , .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT
                         | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
        , .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT };
        vkCmdPipelineBarrier(Frame.m_VKCommandBuffer, Before, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &In, 0, nullptr, 0, nullptr);

        vkCmdBindPipeline(Frame.m_VKCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, C.m_VKPipeline);
        if( Set != VK_NULL_HANDLE ) vkCmdBindDescriptorSets(Frame.m_VKCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, C.m_VKPipelineLayout, 0, 1, &Set, 0, nullptr);
        if( TextureSet != VK_NULL_HANDLE ) vkCmdBindDescriptorSets(Frame.m_VKCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, C.m_VKPipelineLayout, 1, 1, &TextureSet, 0, nullptr);
        if( PushSize ) vkCmdPushConstants(Frame.m_VKCommandBuffer, C.m_VKPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<std::uint32_t>(PushSize), pPush);
        vkCmdDispatch(Frame.m_VKCommandBuffer, static_cast<std::uint32_t>(X), static_cast<std::uint32_t>(Y), static_cast<std::uint32_t>(Z));

        // Later shaders, copies and the host (a readback after the frame) see what it wrote
        const VkMemoryBarrier Out
        { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT
        , .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT };
        vkCmdPipelineBarrier(Frame.m_VKCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, Before, 0, 1, &Out, 0, nullptr, 0, nullptr);
    }
}
