namespace xgpu::vulkan
{
    struct pipeline_instance : xgpu::details::pipeline_instance_handle
    {
        virtual                ~pipeline_instance           ( void 
                                                            ) noexcept;
        xgpu::device::error*    Initialize                  ( std::shared_ptr<vulkan::device>&&     Device
                                                            , const xgpu::pipeline_instance::setup& Setup 
                                                            ) noexcept;

        virtual void            DeathMarch                  ( xgpu::pipeline_instance && buffer) noexcept override;
        struct per_renderpass
        {
            pipeline_instance*              m_pPipelineInstance {};
            std::array<VkDescriptorSet,3>   m_VKDescriptorSet   {};
            VkPipeline                      m_VKPipeline        {}; // Cache this here for performace but this is just a view
        };

        std::shared_ptr<vulkan::pipeline>                   m_Pipeline              {};
        std::array< std::shared_ptr<vulkan::texture>, 16>   m_TexturesBinds         {};
        std::shared_ptr<vulkan::device>                     m_Device                {};

        VkDescriptorSet                                     m_VKSamplerSet{ VK_NULL_HANDLE };
        VkDescriptorSet                                     m_VKDynamicSet{ VK_NULL_HANDLE };
    };
}