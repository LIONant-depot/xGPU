namespace xgpu::vulkan
{
    struct pipeline : xgpu::details::pipeline_handle
    {
        xgpu::device::error* Initialize     ( std::shared_ptr<device>&&    Device
                                            , const xgpu::pipeline::setup& Setup
                                            ) noexcept;
        ~pipeline(void) override;

        int getNStaticUBOs() const noexcept { return static_cast<int>(m_StaticView.size()); }
        int getNDynamicUBOs() const noexcept { return static_cast<int>(m_DynamicView.size()); }
        int getStaticBindIndex(int idx) const noexcept { return (idx < m_StaticView.size()) ? m_StaticView[idx].m_BindIndex : -1; }
        int getDynamicBindIndex(int idx) const noexcept { return (idx < m_DynamicView.size()) ? m_DynamicView[idx].m_BindIndex : -1; }
        
        virtual void              DeathMarch( xgpu::pipeline&& buffer ) noexcept override;

        struct per_renderpass
        {
            pipeline*       m_pPipeline;
            VkPipeline      m_VKPipeline;
        };

        inline static constexpr int sampler_set_index_v = 0;
        inline static constexpr int static_set_index_v = 1;
        inline static constexpr int dynamic_set_index_v = 2;

        using stages_array                 = std::array<std::shared_ptr<xgpu::vulkan::shader>, xgpu::shader::type::count_v>;
        using stages_createinfo_array      = std::array<VkPipelineShaderStageCreateInfo,       xgpu::shader::type::count_v>;
        using samples_array                = std::array<VkSampler, 16>;
        using blend_attachment_state_array = std::array<VkPipelineColorBlendAttachmentState, xgpu::renderpass::max_attachments_v>;

        std::shared_ptr<device>                             m_Device                        {};
        std::shared_ptr<vulkan::vertex_descriptor>          m_VertexDesciptor               {};
        int                                                 m_nVKDescriptorSetLayout        { 0 };
        std::array<VkDescriptorSetLayout, 3>                m_VKDescriptorSetLayout         {};
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
        std::array<xgpu::pipeline::uniform_binds,10>        m_UniformsBinds                 {};
        std::span<xgpu::pipeline::uniform_binds>            m_StaticView                    {};
        std::span<xgpu::pipeline::uniform_binds>            m_DynamicView                   {};
//        std::array<xgpu::shader::type,5>                    m_UniformBufferBits             {};
        int                                                 m_nUniformBuffers               {0};
    };
}