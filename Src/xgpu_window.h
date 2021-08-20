namespace xgpu
{
    struct window
    {
        struct setup
        {
            int                 m_Width         { 1280 };
            int                 m_Height        { 720 };
            bool                m_bFullScreen   { false };
            bool                m_bClearOnRender{ true };
            bool                m_bSyncOn       { false };
            float               m_ClearColorR   { 0.45f };
            float               m_ClearColorG   { 0.45f };
            float               m_ClearColorB   { 0.45f };
            float               m_ClearColorA   { 1.0f };
        };

        VGPU_INLINE [[nodiscard]]   bool            isValid                 ( void ) const noexcept;
        VGPU_INLINE [[nodiscard]]   int             getWidth                ( void ) const noexcept;
        VGPU_INLINE [[nodiscard]]   int             getHeight               ( void ) const noexcept;
        VGPU_INLINE [[nodiscard]]   bool            isMinimized             ( void ) const noexcept;
        VGPU_INLINE                 cmd_buffer      getCmdBuffer            ( void ) noexcept;
        VGPU_INLINE                 void            PageFlip                ( void ) noexcept;
        VGPU_INLINE                 void            setClearColor           ( float R, float G, float B, float A ) noexcept;

        std::shared_ptr<details::window_handle> m_Private{};
    };
}
