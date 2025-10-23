namespace xgpu
{
    struct window
    {
        struct setup
        {
            int                 m_X             { -1 };
            int                 m_Y             { -1 };
            int                 m_Width         { 1280 };
            int                 m_Height        { 720 };
            bool                m_bFullScreen   { false };
            bool                m_bClearOnRender{ true };
            bool                m_bSyncOn       { false };
            bool                m_bFrameless    { false };
            bool                m_bFocus        { true };
            float               m_ClearColorR   { 0.45f };
            float               m_ClearColorG   { 0.45f };
            float               m_ClearColorB   { 0.45f };
            float               m_ClearColorA   { 1.0f };
        };


        XGPU_INLINE                                     window                  ( void ) = default;
        XGPU_INLINE                                    ~window                  ( void ) noexcept;

        XGPU_INLINE [[nodiscard]]   bool                isValid                 ( void ) const noexcept;
        XGPU_INLINE [[nodiscard]]   int                 getWidth                ( void ) const noexcept;
        XGPU_INLINE [[nodiscard]]   int                 getHeight               ( void ) const noexcept;
        XGPU_INLINE [[nodiscard]]   bool                BeginRendering          ( void ) const noexcept;
        XGPU_INLINE                 cmd_buffer          getCmdBuffer            ( void ) noexcept;
        XGPU_INLINE                 cmd_buffer          StartRenderPass         ( const renderpass& Renderpass ) noexcept;
        XGPU_INLINE                 void                PageFlip                ( void ) noexcept;
        XGPU_INLINE                 void                setClearColor           ( float R, float G, float B, float A ) noexcept;
        XGPU_INLINE [[nodiscard]]   std::size_t         getSystemWindowHandle   ( void ) const noexcept;
        XGPU_INLINE [[nodiscard]]   bool                isFocused               ( void ) const noexcept;
        XGPU_INLINE [[nodiscard]]   bool                isCapturing             ( void ) const noexcept;
        XGPU_INLINE                 void                setFocus                ( void ) const noexcept;
        XGPU_INLINE [[nodiscard]]   bool                isMinimized             ( void ) const noexcept;
        XGPU_INLINE [[nodiscard]]   std::pair<int,int>  getPosition             ( void ) const noexcept;
        XGPU_INLINE                 void                setPosition             ( int x, int y ) noexcept;
        XGPU_INLINE                 void                setSize                 ( int Width, int Height) noexcept;
        XGPU_INLINE                 void                setMousePosition        ( int x, int y ) noexcept;
        XGPU_INLINE [[nodiscard]]   void                getDevice               ( xgpu::device& Device ) const noexcept;

        std::shared_ptr<details::window_handle> m_Private{};
    };
}
