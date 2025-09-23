namespace xgpu
{
    struct pipeline_instance
    {
        struct sampler_binding
        {
            xgpu::texture&      m_Value;
        };

        struct uniform_buffer
        {
            xgpu::buffer&       m_Value;
        };

        struct setup
        {
            xgpu::pipeline&             m_PipeLine;
            std::span<uniform_buffer>   m_UniformBuffersBindings    {};
            std::span<sampler_binding>  m_SamplersBindings          {};
        };
        
        std::shared_ptr<details::pipeline_instance_handle> m_Private;
    };
}