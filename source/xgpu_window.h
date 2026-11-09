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
        // Records a GPU copy of Src's mip 0 (a render pass's attachment, resting where the pass left it: depth at its read-only layout, color
        // at shader-read) into Dst from its entry 0, texels packed in rows (a DEPTH_F32's: a float each). Between render passes, as
        // CopyBuffer: it waits for the pass that wrote Src; later shaders and copies see Dst, and a later pass can load or sample Src
        XGPU_INLINE                 void                CopyTextureToBuffer     ( const texture& Src, buffer& Dst ) noexcept;
        // Src's first Width x Height texels (mip 0, colour, at shader-read) into Dst's (the same format), between render passes: it waits
        // for the passes that wrote Src and that read Dst; both are at shader-read after it, later shaders see Dst
        XGPU_INLINE                 void                CopyTexture             ( const texture& Src, texture& Dst, int Width, int Height ) noexcept;
        // A GPU timestamp between render passes (cmd_buffer::WriteTimestamp is the one inside a pass) - see xgpu_timestamps.h
        XGPU_INLINE                 void                WriteTimestamp          ( timestamps& Timestamps, int Index ) noexcept;
        // A section of dispatches (and timestamps) that runs beside the rest of the frame, on a second queue: what is recorded before it is
        // submitted first and the section waits for it; the frame's later work does not wait for the section, the next frame does (before
        // anything of its own). Between render passes; false: no second queue (XGPU_NO_ASYNC, or one queue only) - the dispatches run in
        // the frame as usual. The section must not touch what the rest of this frame uses
        XGPU_INLINE                 bool                BeginAsyncCompute       ( void ) noexcept;
        XGPU_INLINE                 void                EndAsyncCompute         ( void ) noexcept;
        // An explicit-sync section (the render graph's managed scopes, R1a): Begin waits for all earlier work once and puts Images (the
        // storage images the section writes) in the general layout; inside, Dispatch puts in no barriers of its own and leaves its storage
        // images general (sampled reads of them too) - the caller orders the dispatches with ComputeBarrier where one reads or overwrites
        // what an earlier one touched; End makes the section's writes visible to everything after and samples the images again.
        // Dispatches only (no copies or render passes inside)
        XGPU_INLINE                 void                BeginExplicitSync       ( std::span<const texture* const> Images ) noexcept;
        XGPU_INLINE                 void                ComputeBarrier          ( void ) noexcept;
        XGPU_INLINE                 void                EndExplicitSync         ( void ) noexcept;
        // A profiler zone on the GPU between render passes (xprofile's XPROFILE_GPU_SCOPE: pWhere its location); Begin/End in pairs, nested
        // last-in first-out, in this frame. Nothing when the build has no profiler or it is off
        XGPU_INLINE                 void                BeginGPUZone            ( const void* pWhere ) noexcept;
        XGPU_INLINE                 void                EndGPUZone              ( void ) noexcept;
        // Runs Compute over X x Y x Z work groups in this frame's command buffer, at this point of the frame (between render passes, as
        // CopyBuffer): Buffers are its storage buffers in binding order, pPush its push constants. It waits for the earlier work on the
        // buffers (draws, copies, the host's writes, other dispatches); later shaders, copies and a readback after the frame see its writes
        XGPU_INLINE                 void                Dispatch                ( compute& Compute, std::span<buffer* const> Buffers, const void* pPush, std::size_t PushSize, int X, int Y = 1, int Z = 1 ) noexcept;
        // The same with its storage images too (compute::setup::m_nImages): each in the general layout for the dispatch, sampled again after it,
        // where a finished render pass also leaves a target
        XGPU_INLINE                 void                Dispatch                ( compute& Compute, std::span<buffer* const> Buffers, std::span<const texture* const> Textures, std::span<const texture* const> Images, const void* pPush, std::size_t PushSize, int X, int Y = 1, int Z = 1 ) noexcept;
        // The same with its textures (compute::setup::m_nTextures): it also waits for the passes that rendered them
        XGPU_INLINE                 void                Dispatch                ( compute& Compute, std::span<buffer* const> Buffers, std::span<const texture* const> Textures, const void* pPush, std::size_t PushSize, int X, int Y = 1, int Z = 1 ) noexcept;

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
        // Lets go of the mouse the window holds while a button is down (X11's implicit grab), so the window manager can take it: a window that
        // draws its own title bar asks the window manager to move or resize it this way (_NET_WM_MOVERESIZE). Nothing to do on Windows.
        XGPU_INLINE                 void                releaseMouseCapture     ( void ) noexcept;
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
