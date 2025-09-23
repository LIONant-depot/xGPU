namespace xgpu
{
    //--------------------------------------------------------------------------------------

    cmd_buffer::cmd_buffer(cmd_buffer&& CmdBuffer) noexcept
    {
        m_pWindow = CmdBuffer.m_pWindow;
        m_Memory  = CmdBuffer.m_Memory;
        CmdBuffer.m_pWindow = nullptr;
    }

    //--------------------------------------------------------------------------------------

    cmd_buffer::~cmd_buffer( void ) noexcept
    {
        if(m_pWindow) m_pWindow->CmdRenderEnd(*this);
    }

    //--------------------------------------------------------------------------------------

    cmd_buffer& cmd_buffer::setPipelineInstance( xgpu::pipeline_instance& Instance ) noexcept
    {
        m_pWindow->setPipelineInstance(*this, Instance);
        return *this;
    }

    //--------------------------------------------------------------------------------------

    cmd_buffer& cmd_buffer::setBuffer( xgpu::buffer& Buffer, int StartingElementIndex ) noexcept
    {
        m_pWindow->setBuffer(*this, Buffer, StartingElementIndex );
        return *this;
    }

    //--------------------------------------------------------------------------------------
    cmd_buffer& cmd_buffer::setStreamingBuffers(std::span<xgpu::buffer> Buffers, int StartingElementIndex ) noexcept
    {
        m_pWindow->setStreamingBuffers(*this, Buffers, StartingElementIndex);
        return *this;
    }

    //--------------------------------------------------------------------------------------

    cmd_buffer& cmd_buffer::Draw(int IndexCount, int FirstIndex, int VertexOffset ) noexcept
    {
        return DrawInstance(1, IndexCount, 0, FirstIndex, VertexOffset );
    }

    //--------------------------------------------------------------------------------------

    cmd_buffer& cmd_buffer::DrawInstance(int InstanceCount, int IndexCount, int FirstInstance, int FirstIndex, int VertexOffset ) noexcept
    {
        m_pWindow->DrawInstance(*this, InstanceCount, IndexCount, FirstInstance, FirstIndex, VertexOffset );
        return *this;
    }

    //--------------------------------------------------------------------------------------

    cmd_buffer& cmd_buffer::setViewport( float x, float y, float w, float h, float minDepth, float maxDepth ) noexcept
    {
        m_pWindow->setViewport(*this, x,y,w,h,minDepth,maxDepth);
        return *this;
    }

    //--------------------------------------------------------------------------------------

    cmd_buffer& cmd_buffer::setScissor( int x, int y, int w, int h ) noexcept
    {
        m_pWindow->setScissor(*this, x, y, w, h);
        return *this;
    }

    //--------------------------------------------------------------------------------------

    cmd_buffer& cmd_buffer::setPushConstants( const void* pData, std::size_t Size ) noexcept
    {
        m_pWindow->setPushConstants(*this, pData, Size );
        return *this;
    }

    //--------------------------------------------------------------------------------------
    template<typename T>
    cmd_buffer& cmd_buffer::setPushConstants( const T& PushConstants ) noexcept
    {
        static_assert( std::is_reference_v<T> == false && std::is_pointer_v<T> == false );
        m_pWindow->setPushConstants(*this, &PushConstants, sizeof(T));
        return *this;
    }

    //--------------------------------------------------------------------------------------

    void cmd_buffer::Release( void ) noexcept
    {
        if (m_pWindow) m_pWindow->CmdRenderEnd(*this);
        m_pWindow = nullptr;
    }

    //--------------------------------------------------------------------------------------

    template<typename T>
    T& cmd_buffer::getUniformBufferVMem(xgpu::shader::type::bit ShaderType) noexcept
    {
        return *reinterpret_cast<T*>(m_pWindow->getUniformBufferVMem( *this, ShaderType, sizeof(T) ));
    }

    //--------------------------------------------------------------------------------------
    cmd_buffer& cmd_buffer::setDepthBias(float ConstantFactor, float DepthBiasClamp, float DepthBiasSlope) noexcept
    {
        m_pWindow->setDepthBias(*this, ConstantFactor, DepthBiasClamp, DepthBiasSlope);
        return *this;
    }
}