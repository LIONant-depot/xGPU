namespace xgpu
{
    struct pipeline_instance
    {
        struct sampler_binding
        {
            xgpu::texture&      m_Texture;
        };

        struct setup
        {
            xgpu::pipeline&                 m_PipeLine;
            std::span<sampler_binding>      m_SamplersBindings;
        };

        std::shared_ptr<details::pipeline_instance_handle> m_Private;
    };
}