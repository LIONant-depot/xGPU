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
            std::span<sampler_binding>  m_SamplersBindings  {};
            std::span<uniform_buffer>   m_UniformBuffersBindings    {};
        };

        template<typename T>
        T& getUniformBufferVMem( xgpu::shader::type::bit ShaderType ) noexcept;

        std::shared_ptr<details::pipeline_instance_handle> m_Private;
    };
}