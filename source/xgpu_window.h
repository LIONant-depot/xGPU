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

        // Registers Dest/Width/Height to receive the current frame's back-buffer pixels - one packed
        // uint32 per pixel (B8G8R8A8 byte order in memory, so 0xAARRGGBB when read as a hex integer on
        // a little-endian machine), row-major top-to-bottom. The pixels aren't valid the moment this
        // call returns: the copy can only safely happen once this frame's GPU work is fence-known-
        // complete and still application-owned, which is one specific point inside PageFlip() - so
        // call this any time before the PageFlip() you want captured, then read Dest/Width/Height only
        // AFTER that PageFlip() call returns. What to do with the pixels (build an xbitmap, hand them
        // to some other system) is deliberately left to the caller - see source/Tools/
        // xgpu_screenshot.h for an xbitmap-wrapping convenience, since the engine core itself has no
        // dependency on xbitmap.
        XGPU_INLINE [[nodiscard]]   bool                Screenshot              ( std::vector<std::uint32_t>& Dest, int& Width, int& Height ) noexcept;
        XGPU_INLINE                 void                setClearColor           ( float R, float G, float B, float A ) noexcept;
        XGPU_INLINE [[nodiscard]]   std::size_t         getSystemWindowHandle   ( void ) const noexcept;
        XGPU_INLINE [[nodiscard]]   bool                isFocused               ( void ) const noexcept;
        XGPU_INLINE [[nodiscard]]   bool                isCapturing             ( void ) const noexcept;
        XGPU_INLINE [[nodiscard]]   bool                isHovered               ( void ) const noexcept;
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
