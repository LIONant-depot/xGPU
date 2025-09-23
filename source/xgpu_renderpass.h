namespace xgpu
{
    struct renderpass
    {
        constexpr static int max_attachments_v = 8;

        struct attachment
        {
            xgpu::texture           m_Texture;
        };

        struct setup
        {
            std::span<attachment>   m_Attachments       {};
            bool                    m_bClearOnRender    { true };
            float                   m_ClearColorR       { 0.45f };
            float                   m_ClearColorG       { 0.45f };
            float                   m_ClearColorB       { 0.45f };
            float                   m_ClearColorA       { 1.0f };
            float                   m_ClearDepthFar     { 1.0f };
            std::uint32_t           m_ClearStencil      { 0 };
        };

        std::shared_ptr<details::renderpass_handle> m_Private;
    };
}