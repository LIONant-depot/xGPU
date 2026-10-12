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

        [[nodiscard]] XGPU_INLINE bool                isValid                 ( void ) const noexcept;
        [[nodiscard]] XGPU_INLINE int                 getWidth                ( void ) const noexcept;
        [[nodiscard]] XGPU_INLINE int                 getHeight               ( void ) const noexcept;
        [[nodiscard]] XGPU_INLINE bool                BeginRendering          ( void ) const noexcept;
        XGPU_INLINE                 cmd_buffer          getCmdBuffer            ( void ) noexcept;
        XGPU_INLINE                 cmd_buffer          StartRenderPass         ( const renderpass& Renderpass ) noexcept;
        XGPU_INLINE                 void                PageFlip                ( void ) noexcept;
        // Records a GPU copy of Regions from Src (a CPU_WRITE_GPU_READ buffer) into Dst in this frame's command buffer, at
        // this point of the frame: call it between render passes (not inside a StartRenderPass scope). The copy waits for the
        // shader reads of earlier commands (frames still in flight included), and later shaders see its result. For
        // keeping a device-local (GPU_READ) buffer up to date with deltas written by the CPU into a ring.
        XGPU_INLINE                 void                CopyBuffer              ( const buffer& Src, buffer& Dst, std::span<const buffer::copy> Regions ) noexcept;
        // A GPU timestamp between render passes (cmd_buffer::WriteTimestamp is the one inside a pass) - see xgpu_timestamps.h
        XGPU_INLINE                 void                WriteTimestamp          ( timestamps& Timestamps, int Index ) noexcept;

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
        [[nodiscard]] XGPU_INLINE bool                Screenshot              ( std::vector<std::uint32_t>& Dest, int& Width, int& Height ) noexcept;

        // Same idea as Screenshot(), generalized to an arbitrary texture instead of only this window's own
        // back-buffer: a texture rendered into via StartRenderPass shares THIS frame's own command buffer,
        // so it is only actually GPU-complete at the exact point PageFlip() itself already waits on this
        // frame's fence (the same safe point Screenshot()'s own copy runs at) - reading it back any earlier
        // (e.g. immediately after the StartRenderPass draw, via a separate synchronous path) races the real
        // render and is undefined. Call this any time before the PageFlip() you want it captured after; bDone
        // is set to false immediately and flips true once that PageFlip() has actually performed the copy -
        // poll it instead of assuming any particular frame count, since a caller may not control when PageFlip
        // itself is invoked (e.g. code running mid-frame inside a UI panel, as opposed to Screenshot()'s own
        // typical caller - the main loop right before it calls PageFlip() itself).
        [[nodiscard]] XGPU_INLINE bool                ReadbackTexture         ( const texture& Texture, std::vector<std::uint32_t>& Dest, int& Width, int& Height, bool& bDone ) noexcept;
        XGPU_INLINE                 void                setClearColor           ( float R, float G, float B, float A ) noexcept;
        // The OS window's title (UTF-8): e.g. a frame-rate readout
        XGPU_INLINE                 void                setTitle                ( std::string_view Title ) noexcept;
        [[nodiscard]] XGPU_INLINE std::size_t         getSystemWindowHandle   ( void ) const noexcept;
        [[nodiscard]] XGPU_INLINE bool                isFocused               ( void ) const noexcept;
        [[nodiscard]] XGPU_INLINE bool                isCapturing             ( void ) const noexcept;
        [[nodiscard]] XGPU_INLINE bool                isHovered               ( void ) const noexcept;
        XGPU_INLINE                 void                setFocus                ( void ) const noexcept;
        [[nodiscard]] XGPU_INLINE bool                isMinimized             ( void ) const noexcept;
        [[nodiscard]] XGPU_INLINE std::pair<int,int>  getPosition             ( void ) const noexcept;
        XGPU_INLINE                 void                setPosition             ( int x, int y ) noexcept;
        XGPU_INLINE                 void                setSize                 ( int Width, int Height) noexcept;
        XGPU_INLINE                 void                setMousePosition        ( int x, int y ) noexcept;
        [[nodiscard]] XGPU_INLINE void                getDevice               ( xgpu::device& Device ) const noexcept;

        std::shared_ptr<details::window_handle> m_Private{};
    };
}
