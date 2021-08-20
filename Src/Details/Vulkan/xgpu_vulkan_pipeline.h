namespace xgpu::vulkan
{
    struct pipeline : xgpu::details::pipeline_handle
    {
        xgpu::device::error* Initialize     ( std::shared_ptr<device>&&    Device
                                            , const xgpu::pipeline::setup& Setup
                                            ) noexcept;

        using stages_array              = std::array<std::shared_ptr<xgpu::vulkan::shader>, (int)xgpu::shader::type::ENUM_COUNT>;
        using stages_createinfo_array   = std::array<VkPipelineShaderStageCreateInfo,       (int)xgpu::shader::type::ENUM_COUNT>;
        using samples_array             = std::array<VkSampler, 16>;

        std::shared_ptr<device>                             m_Device                        {};
        int                                                 m_nVKDescriptorSetLayout        { 0 };
        std::array<VkDescriptorSetLayout, 1>                m_VKDescriptorSetLayout         {};
        VkPipelineLayout                                    m_VKPipelineLayout              {};
        VkGraphicsPipelineCreateInfo                        m_VkPipelineCreateInfo          {};
        VkPipelineInputAssemblyStateCreateInfo              m_VkInputAssemblyState          {};
        VkPipelineColorBlendStateCreateInfo                 m_VkColorBlendState             {};
        std::array<VkPipelineColorBlendAttachmentState, 1>  m_lVkBlendAttachmentState       {};
        VkPipelineRasterizationStateCreateInfo              m_VkRasterizationState          {};
        VkPipelineMultisampleStateCreateInfo                m_VkMultisampleState            {};
        VkPipelineDepthStencilStateCreateInfo               m_VkDepthStencilState           {};
        VkPipelineViewportStateCreateInfo                   m_VKViewportInfo                {};
        std::array<VkDynamicState,2>                        m_VKDyanmicStates               {};
        VkPipelineDynamicStateCreateInfo                    m_VKDynamicStateCreateInfo      {};
        mutable lock_object<VkDescriptorPool>               m_LockedVKDescriptorPool        {};
        int                                                 m_nShaderStages                 { 0 };
        int                                                 m_nSamplers                     { 0 };
        stages_array                                        m_ShaderStages                  {};
        samples_array                                       m_VKSamplers                    {};
        stages_createinfo_array                             m_ShaderStagesCreateInfo        {};
        std::atomic_int                                     m_nPipelineInstances            {0};
    };
}