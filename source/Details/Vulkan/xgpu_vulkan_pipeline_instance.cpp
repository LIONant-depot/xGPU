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
            VkDescriptorSetAllocateInfo alloc{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = m_Device->m_LockedVKDescriptorPool.get(),
                .descriptorSetCount = 1,
                .pSetLayouts = &m_Pipeline->m_VKDescriptorSetLayout[pipeline::sampler_set_index_v]
            };
            if (auto err = vkAllocateDescriptorSets(m_Device->m_VKDevice, &alloc, &allocatedSet)) {
                m_Device->m_Instance->ReportError(err, "vkAllocateDescriptorSets");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to allocate sampler descriptor set");
            }
            m_VKSamplerSet = allocatedSet;
        }
        else
        {
            m_VKSamplerSet = m_Device->m_DescriptorRings[VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER].alloc(m_Device->m_VKDevice, m_Pipeline->m_VKDescriptorSetLayout[pipeline::sampler_set_index_v]);
            assert(m_VKSamplerSet != VK_NULL_HANDLE);  // Add this assert
        }

        //
        // Allocate/update descriptor set for samplers (set 0) if present
        //
        if (m_Pipeline->m_nSamplers > 0)
        {
            // Update the set
            std::array<VkWriteDescriptorSet, 16>   writes{};
            std::array<VkDescriptorImageInfo, 16>  imageInfos{};

            for (int i = 0; i < m_Pipeline->m_nSamplers; ++i)
            {
                imageInfos[i]         = m_TexturesBinds[i]->m_VKDescriptorImageInfo;
                imageInfos[i].sampler = m_Pipeline->m_VKSamplers[i];

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
