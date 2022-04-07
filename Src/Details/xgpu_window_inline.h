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
            virtual     void                            CmdRenderBegin          ( xgpu::cmd_buffer& CmdBuffer )                             noexcept = 0;
            virtual     void                            CmdRenderBegin          ( xgpu::cmd_buffer& CmdBuffer, const xgpu::renderpass& Renderpass )                      noexcept = 0;
            virtual     void                            CmdRenderEnd            ( xgpu::cmd_buffer& CmdBuffer )                             noexcept = 0;
            virtual     bool                            BegingRendering         ( void )                                                    noexcept = 0;
            virtual     void                            setPipelineInstance     ( xgpu::cmd_buffer& CmdBuffer, xgpu::pipeline_instance& Instance )                       noexcept = 0;
            virtual     void                            setBuffer               ( xgpu::cmd_buffer& CmdBuffer, xgpu::buffer& Buffer, int StartingElementIndex )          noexcept = 0;
            virtual     void                            setStreamingBuffers     ( xgpu::cmd_buffer& CmdBuffer, std::span<xgpu::buffer> Buffers, int StartingElementIndex)noexcept = 0;
            virtual     void                            DrawInstance            ( xgpu::cmd_buffer& CmdBuffer, int InstanceCount, int IndexCount, int FirstInstance, int FirstIndex, int VertexOffset ) noexcept = 0;
            virtual     void                            setViewport             ( xgpu::cmd_buffer& CmdBuffer, float x, float y, float w, float h, float minDepth, float maxDepth ) noexcept = 0;
            virtual     void                            setScissor              ( xgpu::cmd_buffer& CmdBuffer, int x, int y, int w, int h )                              noexcept = 0;
            virtual     void                            setPushConstants        ( xgpu::cmd_buffer& CmdBuffer, const void* pData, std::size_t Size )                     noexcept = 0;
            virtual     void*                           getUniformBufferVMem    ( xgpu::cmd_buffer& CmdBuffer, xgpu::shader::type::bit ShaderType, std::size_t Size )    noexcept = 0;
            virtual     void                            setDepthBias            ( xgpu::cmd_buffer& CmdBuffer, float ConstantFactor, float DepthBiasClamp, float DepthBiasSlope) noexcept = 0;

            virtual     void                            setClearColor           ( float R, float G, float B, float A )                      noexcept = 0;
            virtual     std::size_t                     getSystemWindowHandle   ( void ) const                                              noexcept = 0;
            virtual     bool                            isFocused               ( void ) const                                              noexcept = 0;
            virtual     std::pair<int, int>             getPosition             ( void ) const                                              noexcept = 0;
            virtual     xgpu::device                    getDevice               ( void ) const                                              noexcept = 0;


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

    XGPU_INLINE
    bool window::isValid(void) const noexcept
    {
        return m_Private.get() != nullptr;
    }

    //--------------------------------------------------------------------------

    XGPU_INLINE
    int window::getWidth(void) const noexcept
    {
        return m_Private->getWidth();
    }

    //--------------------------------------------------------------------------

    XGPU_INLINE
    int window::getHeight(void) const noexcept
    {
        return m_Private->getHeight();
    }

    //--------------------------------------------------------------------------
    XGPU_INLINE
    void window::PageFlip(void) noexcept
    {
        m_Private->PageFlip();
    }

    //--------------------------------------------------------------------------

    XGPU_INLINE
    cmd_buffer window::getCmdBuffer( void ) noexcept
    {
        cmd_buffer CmdBuffer{};
        m_Private->CmdRenderBegin(CmdBuffer);
        return CmdBuffer;
    }

    //--------------------------------------------------------------------------

    XGPU_INLINE
    cmd_buffer window::StartRenderPass( const renderpass& Renderpass ) noexcept
    {
        cmd_buffer CmdBuffer{};
        m_Private->CmdRenderBegin(CmdBuffer, Renderpass);
        return CmdBuffer;
    }

    //--------------------------------------------------------------------------

    XGPU_INLINE
    [[nodiscard]] bool
    window::BeginRendering(void) const noexcept
    {
        if( m_Private->BegingRendering() ) return true;
        return false;
    }

    //--------------------------------------------------------------------------

    XGPU_INLINE
    void window::setClearColor( float R, float G, float B, float A) noexcept
    {
        return m_Private->setClearColor(R,G,B,A);
    }

    //--------------------------------------------------------------------------

    XGPU_INLINE
    [[nodiscard]] std::size_t
    window::getSystemWindowHandle( void ) const noexcept
    {
        return m_Private->getSystemWindowHandle();
    }

    //--------------------------------------------------------------------------

    XGPU_INLINE [[nodiscard]] bool
    window::isFocused(void) const noexcept
    {
        return m_Private->isFocused();
    }

    //--------------------------------------------------------------------------

    XGPU_INLINE [[nodiscard]]
    std::pair<int, int> window::getPosition(void) const noexcept
    {
        return m_Private->getPosition();
    }

    //--------------------------------------------------------------------------

    XGPU_INLINE [[nodiscard]]
    void window::getDevice( xgpu::device& Device ) const noexcept
    {
        Device = m_Private->getDevice();
    }
}
