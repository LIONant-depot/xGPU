namespace xgpu
{
    //--------------------------------------------------------------------------------------

    cmd_buffer::cmd_buffer( details::window_handle* pWindowHandle ) noexcept
        : m_pWindow{ pWindowHandle }
    {}

    //--------------------------------------------------------------------------------------

    cmd_buffer::cmd_buffer( cmd_buffer&& CmdBuffer ) noexcept
    {
        assert(m_pWindow == nullptr );
        m_pWindow = CmdBuffer.m_pWindow;
        CmdBuffer.m_pWindow = nullptr;
    }

    //--------------------------------------------------------------------------------------

    cmd_buffer::~cmd_buffer( void ) noexcept
    {
        if(m_pWindow) m_pWindow->CmdRenderEnd();
    }

    //--------------------------------------------------------------------------------------

    cmd_buffer& cmd_buffer::setPipelineInstance( xgpu::pipeline_instance& Instance ) noexcept
    {
        m_pWindow->setPipelineInstance(Instance);
        return *this;
    }

    //--------------------------------------------------------------------------------------

    cmd_buffer& cmd_buffer::setBuffer( xgpu::buffer& Buffer, int StartingElementIndex ) noexcept
    {
        m_pWindow->setBuffer( Buffer, StartingElementIndex );
        return *this;
    }

    //--------------------------------------------------------------------------------------
    cmd_buffer& cmd_buffer::setStreamingBuffers(std::span<xgpu::buffer> Buffers, int StartingElementIndex ) noexcept
    {
        m_pWindow->setStreamingBuffers(Buffers, StartingElementIndex);
        return *this;
    }

    //--------------------------------------------------------------------------------------

    cmd_buffer& cmd_buffer::Draw(int IndexCount, int FirstIndex, int VertexOffset ) noexcept
    {
        return DrawInstance( 1, IndexCount, 0, FirstIndex, VertexOffset );
    }

    //--------------------------------------------------------------------------------------

    cmd_buffer& cmd_buffer::DrawInstance(int InstanceCount, int IndexCount, int FirstInstance, int FirstIndex, int VertexOffset ) noexcept
    {
        m_pWindow->DrawInstance( InstanceCount, IndexCount, FirstInstance, FirstIndex, VertexOffset );
        return *this;
    }

    //--------------------------------------------------------------------------------------

    cmd_buffer& cmd_buffer::setViewport( float x, float y, float w, float h, float minDepth, float maxDepth ) noexcept
    {
        m_pWindow->setViewport(x,y,w,h,minDepth,maxDepth);
        return *this;
    }

    //--------------------------------------------------------------------------------------

    cmd_buffer& cmd_buffer::setScissor( int x, int y, int w, int h ) noexcept
    {
        m_pWindow->setScissor(x, y, w, h);
        return *this;
    }

    //--------------------------------------------------------------------------------------

    cmd_buffer& cmd_buffer::setConstants( xgpu::shader::type Type, int Offset, const void* pData, int Size ) noexcept
    {
        m_pWindow->setConstants( Type, Offset, pData, Size );
        return *this;
    }

    //--------------------------------------------------------------------------------------

    void cmd_buffer::Release( void ) noexcept
    {
        if (m_pWindow) m_pWindow->CmdRenderEnd();
        m_pWindow = nullptr;
    }
}