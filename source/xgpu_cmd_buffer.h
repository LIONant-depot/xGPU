namespace xgpu
{
    struct cmd_buffer
    {
                                        cmd_buffer                  ( void )                                noexcept = default;
        XGPU_INLINE                     cmd_buffer                  ( cmd_buffer&& CmdBuffer )              noexcept;
        XGPU_INLINE                    ~cmd_buffer                  ( void )                                noexcept;
        XGPU_INLINE     cmd_buffer&     setPipelineInstance         ( xgpu::pipeline_instance& Instance )   noexcept;
        XGPU_INLINE     cmd_buffer&     setStreamingBuffers         ( std::span<xgpu::buffer> Buffers, int StartingElementIndex=0) noexcept;
        XGPU_INLINE     cmd_buffer&     setBuffer                   ( xgpu::buffer& Buffer, int StartingElementIndex=0) noexcept;
        XGPU_INLINE     cmd_buffer&     setViewport                 ( float x, float y, float w, float h, float minDepth = 0.0f, float maxDepth = 1.0f) noexcept;
        XGPU_INLINE     cmd_buffer&     setScissor                  ( int x, int y, int w, int h )          noexcept;
        template<typename T>
        XGPU_INLINE     cmd_buffer&     setPushConstants            ( const T& PushConstants ) noexcept;
        XGPU_INLINE     cmd_buffer&     setPushConstants            ( const void* pData, std::size_t Size ) noexcept;
        XGPU_INLINE     cmd_buffer&     Draw                        ( int IndexCount, int FirstIndex = 0, int VertexOffset=0 ) noexcept;
        XGPU_INLINE     cmd_buffer&     DrawInstance                ( int InstanceCount, int IndexCount, int FirstInstance=0, int FirstIndex=0, int VertexOffset=0 ) noexcept;
        XGPU_INLINE     cmd_buffer&     setDepthBias                ( float ConstantFactor, float DepthBiasClamp, float DepthBiasSlope) noexcept;

        template<typename T>
        XGPU_INLINE     T&              getUniformBufferVMem        ( xgpu::shader::type::bit ShaderType ) noexcept;

        XGPU_INLINE     void            Release                     ( void )                                noexcept;

        details::window_handle*         m_pWindow { nullptr };
        std::array<std::size_t, 8>      m_Memory;
    };
}