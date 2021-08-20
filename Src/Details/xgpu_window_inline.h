namespace xgpu
{
    namespace details
    {
        struct window_handle
        {
                                                        window_handle           ( void )                                                    noexcept = default;
            virtual                                    ~window_handle           ( void )                                                    noexcept = default;
            virtual     int                             getWidth                ( void ) const                                              noexcept = 0;
            virtual     int                             getHeight               ( void ) const                                              noexcept = 0;
            virtual     void                            PageFlip                ( void )                                                    noexcept = 0;
            virtual     void                            RenderBegin             ( void )                                                    noexcept = 0;
            virtual     void                            RenderEnd               ( void )                                                    noexcept = 0;
            virtual     bool                            isMinimized             ( void ) const                                              noexcept = 0;
            virtual     void                            setPipelineInstance     ( xgpu::pipeline_instance& Instance )                       noexcept = 0;
            virtual     void                            setBuffer               ( xgpu::buffer& Buffer, int StartingElementIndex )          noexcept = 0;
            virtual     void                            DrawInstance            ( int InstanceCount, int IndexCount, int FirstInstance, int FirstIndex, int VertexOffset ) noexcept = 0;
            virtual     void                            setViewport             ( float x, float y, float w, float h, float minDepth, float maxDepth ) noexcept = 0;
            virtual     void                            setScissor              ( int x, int y, int w, int h )                              noexcept = 0;
            virtual     void                            setConstants            ( xgpu::shader::type Type, int Offset, void* pData, int Size ) noexcept = 0;
            virtual     void                            setClearColor           ( float R, float G, float B, float A )                      noexcept = 0;

//            virtual     bool                            HandleEvents(void)                                                    noexcept = 0;
            //            virtual     void                            BeginRender             ( const eng_view& View, const bool bUpdateViewPort = true ) noexcept = 0;
//            virtual     void                            EndRender               ( void )                                                    noexcept = 0;
//            virtual     eng_device&                     getDevice               ( void )                                                    noexcept = 0;
//            virtual     eng_display_cmds&               getDisplayCmdList       ( const f32 Order )                                         noexcept = 0;
//            virtual     void                            SubmitDisplayCmdList    ( eng_display_cmds& CmdList )                               noexcept = 0;
//            virtual     const eng_view&                 getActiveView           ( void ) const                                              noexcept = 0;
//            virtual     u64                             getSystemWindowHandle   ( void ) const                                              noexcept = 0;
//            virtual     const xgpu::keyboard&      getLocalKeyboard        ( void ) const                                              noexcept = 0;
//            virtual     const xgpu::mouse&         getLocalMouse           ( void ) const                                              noexcept = 0;
//            virtual     void                            setBgColor              ( xcolor C )                                                noexcept = 0;
        };
    }

    //--------------------------------------------------------------------------

    VGPU_INLINE
    bool window::isValid(void) const noexcept
    {
        return m_Private.get() != nullptr;
    }

    //--------------------------------------------------------------------------

    VGPU_INLINE
    int window::getWidth(void) const noexcept
    {
        return m_Private->getWidth();
    }

    //--------------------------------------------------------------------------

    VGPU_INLINE
    int window::getHeight(void) const noexcept
    {
        return m_Private->getHeight();
    }

    //--------------------------------------------------------------------------
    VGPU_INLINE
    void window::PageFlip(void) noexcept
    {
        m_Private->PageFlip();
    }

    //--------------------------------------------------------------------------

    VGPU_INLINE
    cmd_buffer window::getCmdBuffer( void ) noexcept
    {
        m_Private->RenderBegin();
        return {m_Private.get()};
    }

    //--------------------------------------------------------------------------

    VGPU_INLINE
    [[nodiscard]] bool
    window::isMinimized(void) const noexcept
    {
        return m_Private->isMinimized();
    }

    //--------------------------------------------------------------------------

    VGPU_INLINE
    void window::setClearColor( float R, float G, float B, float A) noexcept
    {
        return m_Private->setClearColor(R,G,B,A);
    }


}
