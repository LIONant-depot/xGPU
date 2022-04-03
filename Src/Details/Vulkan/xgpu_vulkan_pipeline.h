namespace xgpu::vulkan
{
    struct pipeline : xgpu::details::pipeline_handle
    {
        xgpu::device::error* Initialize     ( std::shared_ptr<device>&&    Device
                                            , const xgpu::pipeline::setup& Setup
                                            ) noexcept;

        struct per_renderpass
        {
            pipeline*       m_pPipeline;
            VkPipeline      m_VKPipeline;
        };

        using stages_array                 = std::array<std::shared_ptr<xgpu::vulkan::shader>, xgpu::shader::type::count_v>;
        using stages_createinfo_array      = std::array<VkPipelineShaderStageCreateInfo,       xgpu::shader::type::count_v>;
        using samples_array                = std::array<VkSampler, 16>;
        using blend_attachment_state_array = std::array<VkPipelineColorBlendAttachmentState, xgpu::renderpass::max_attachments_v>;
        std::shared_ptr<device>                             m_Device                        {};
        std::shared_ptr<vulkan::vertex_descriptor>          m_VertexDesciptor               {};
        int                                                 m_nVKDescriptorSetLayout        { 0 };
        std::array<VkDescriptorSetLayout, 2>                m_VKDescriptorSetLayout         {};
        VkPipelineLayout                                    m_VKPipelineLayout              {};
        VkGraphicsPipelineCreateInfo                        m_VkPipelineCreateInfo          {};
        VkPipelineInputAssemblyStateCreateInfo              m_VkInputAssemblyState          {};
        VkPipelineColorBlendStateCreateInfo                 m_VkColorBlendState             {};
        blend_attachment_state_array                        m_lVkBlendAttachmentState       {};
        VkPipelineRasterizationStateCreateInfo              m_VkRasterizationState          {};
        VkPipelineMultisampleStateCreateInfo                m_VkMultisampleState            {};
        VkPipelineDepthStencilStateCreateInfo               m_VkDepthStencilState           {};
        VkPipelineViewportStateCreateInfo                   m_VKViewportInfo                {};
        std::array<VkDynamicState,2>                        m_VKDyanmicStates               {};
        VkPipelineDynamicStateCreateInfo                    m_VKDynamicStateCreateInfo      {};
        int                                                 m_nShaderStages                 { 0 };
        int                                                 m_nSamplers                     { 0 };
        stages_array                                        m_ShaderStages                  {};
        samples_array                                       m_VKSamplers                    {};
        stages_createinfo_array                             m_ShaderStagesCreateInfo        {};
        std::atomic_int                                     m_nPipelineInstances            {0};
        std::array<xgpu::shader::type,5>                    m_UniformBufferBits             {};
        int                                                 m_nUniformBuffers               {0};
        std::array<std::uint8_t, xgpu::shader::type::count_v> m_UniformBufferFastRemap      {};
    };
}