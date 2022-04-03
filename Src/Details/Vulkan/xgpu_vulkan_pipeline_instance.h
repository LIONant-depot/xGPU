namespace xgpu::vulkan
{
    struct pipeline_instance : xgpu::details::pipeline_instance_handle
    {
        virtual                ~pipeline_instance           ( void 
                                                            ) noexcept;
        xgpu::device::error*    Initialize                  ( std::shared_ptr<vulkan::device>&&     Device
                                                            , const xgpu::pipeline_instance::setup& Setup 
                                                            ) noexcept;

        void*                   getUniformBufferVMem        ( std::uint32_t&            DynamicOffset
                                                            , xgpu::shader::type::bit   ShaderType
                                                            , std::size_t               Size
                                                            ) noexcept;

        
        struct per_renderpass
        {
            pipeline_instance*              m_pPipelineInstance {};
            std::array<VkDescriptorSet,2>   m_VKDescriptorSet   {};
            VkPipeline                      m_VKPipeline        {}; // Cache this here for performace but this is just a view
        };

        std::shared_ptr<vulkan::pipeline>                   m_Pipeline              {};
        std::array< std::shared_ptr<vulkan::buffer>, 5>     m_UniformBinds          {};
        std::array< std::shared_ptr<vulkan::texture>, 16>   m_TexturesBinds         {};
        std::shared_ptr<vulkan::device>                     m_Device                {};
    };
}