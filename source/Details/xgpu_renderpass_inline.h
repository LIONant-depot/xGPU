namespace xgpu
{
    namespace details
    {
        struct renderpass_handle
        {
            virtual void                                DeathMarch(xgpu::renderpass&& RenderPass)                                  noexcept = 0;
            virtual                                    ~renderpass_handle(void)                                                    noexcept = default;
        };
    }

    inline renderpass::~renderpass(void)
    {
        if (m_Private.use_count() > 1) return;
        if (!m_Private) return;
        m_Private->DeathMarch(std::move(*this));
        m_Private.reset();
    }

}