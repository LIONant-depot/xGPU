namespace xgpu::vulkan
{
    //-------------------------------------------------------------------------------

    pipeline_instance::~pipeline_instance( void ) noexcept
    {
        int a=0;
    }

    //-------------------------------------------------------------------------------

    xgpu::device::error* pipeline_instance::Initialize
    ( std::shared_ptr<vulkan::device>&&         Device
    , const xgpu::pipeline_instance::setup&     Setup
    ) noexcept
    {
        m_Device   = Device;
        m_Pipeline = std::reinterpret_pointer_cast<vulkan::pipeline>(Setup.m_PipeLine.m_Private);

        if (Setup.m_SamplersBindings.size() != m_Pipeline->m_nSamplers)
        {
            m_Device->m_Instance->ReportError("You did not give the expected number of textures bindings as required by the pipeline");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "You did not give the expected number of textures bindings as required by the pipeline");
        }

        //
        // Back up all the sampler bindings
        //
        for (int i = 0; i < m_Pipeline->m_nSamplers; ++i)
        {
            auto& TextureBinds = m_TexturesBinds[i];
            TextureBinds = std::reinterpret_pointer_cast<vulkan::texture>(Setup.m_SamplersBindings[i].m_Value.m_Private);
        }

        if (true)
        {
            std::lock_guard Lk(m_Device->m_LockedVKDescriptorPool);
            VkDescriptorSet allocatedSet = VK_NULL_HANDLE;
            if (auto err = m_Device->AllocateSamplerDescriptorSet(
                    m_Pipeline->m_VKDescriptorSetLayout[pipeline::sampler_set_index_v],
                    allocatedSet,
                    m_VKSamplerPool))
            {
                m_Device->m_Instance->ReportError(err, "vkAllocateDescriptorSets");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to allocate sampler descriptor set");
            }
            m_VKSamplerSet = allocatedSet;
        }

        //
        // Allocate/update descriptor set for samplers (set 0) if present
        //
        if (m_Pipeline->m_nSamplers > 0)
        {
            // Update the set
            std::array<VkWriteDescriptorSet, pipeline::max_samplers_v>   writes{};
            std::array<VkDescriptorImageInfo, pipeline::max_samplers_v>  imageInfos{};

            VkDescriptorImageInfo DummyImage{};
            if( auto* pWhite = static_cast<vulkan::texture*>(m_Device->m_Heap.m_White.m_Private.get()) )
                DummyImage = pWhite->m_VKDescriptorImageInfo;
            const VkSampler DummySampler = m_Device->m_Heap.m_DefaultSampler;

            for (int i = 0; i < m_Pipeline->m_nSamplers; ++i)
            {
                // Dozen inspects each combined-image-sampler's image view at bind
                // (dzn+0x136a6b / +0x136a6f ? SIGSEGV at 0x8 / 0x30 if the view or its
                // inner object is null). vkUpdateDescriptorSets with a null view is
                // skipped there, so the slot would stay unwritten. A dummy 1x1 view
                // (and sampler) keeps every binding valid on every device.
                if( m_TexturesBinds[i] && m_TexturesBinds[i]->m_VKDescriptorImageInfo.imageView != VK_NULL_HANDLE )
                    imageInfos[i] = m_TexturesBinds[i]->m_VKDescriptorImageInfo;
                else
                    imageInfos[i] = DummyImage;
                imageInfos[i].sampler = m_Pipeline->m_VKSamplers[i] != VK_NULL_HANDLE
                                      ? m_Pipeline->m_VKSamplers[i]
                                      : DummySampler;

                writes[i] = VkWriteDescriptorSet
                { .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
                , .dstSet           = m_VKSamplerSet
                , .dstBinding       = static_cast<uint32_t>(i)
                , .descriptorCount  = 1
                , .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                , .pImageInfo       = &imageInfos[i]
                };
            }

            vkUpdateDescriptorSets(m_Device->m_VKDevice, m_Pipeline->m_nSamplers, writes.data(), 0, nullptr);
        }


        return nullptr;
    }

    //-------------------------------------------------------------------------------

    void pipeline_instance::DeathMarch(xgpu::pipeline_instance&& PipelineInstance) noexcept
    {
        if (!m_Device) return;
        m_Device->Destroy(std::move(PipelineInstance));
        m_Device.reset();
    }
}
