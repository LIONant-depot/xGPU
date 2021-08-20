namespace xgpu::vulkan
{
    struct pipeline_instance : xgpu::details::pipeline_instance_handle
    {
        virtual                ~pipeline_instance           ( void 
                                                            ) noexcept;
        xgpu::device::error*    Initialize                  ( std::shared_ptr<vulkan::device>&&     Device
                                                            , const xgpu::pipeline_instance::setup& Setup 
                                                            ) noexcept;

        struct per_renderpass
        {
            pipeline_instance*  m_pPipelineInstance;
            VkDescriptorSet     m_VKDescriptorSet;
            VkPipeline          m_VKPipeline;
        };

        std::shared_ptr<vulkan::device>                     m_Device        {};
        std::shared_ptr<vulkan::pipeline>                   m_Pipeline      {};
        std::array< std::shared_ptr<vulkan::texture>, 16>   m_TexturesBinds {};
    };
}