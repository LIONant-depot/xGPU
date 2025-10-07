namespace xgpu
{
    namespace details
    {
        struct pipeline_instance_handle
        {
            virtual                                    ~pipeline_instance_handle(void)                                                  noexcept = default;
            virtual void                                DeathMarch(xgpu::pipeline_instance&&)                                           noexcept = 0;
        };
    }

    pipeline_instance::~pipeline_instance()
    {
        if (m_Private.use_count() > 1) return;
        if (!m_Private) return;
        m_Private->DeathMarch(std::move(*this));
        m_Private.reset();
    }
}