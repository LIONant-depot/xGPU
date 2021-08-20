namespace xgpu
{
    struct renderpass
    {
        enum class format : std::uint8_t
        { R8G8B8A8
        };

        struct setup
        {
            format      m_OutputFormat  { format::R8G8B8A8 };
            bool        m_bClearOnRender{ true };
        };

        std::shared_ptr<xgpu::details::renderpass_handle> m_Private;
    };
}