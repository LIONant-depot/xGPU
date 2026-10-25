namespace xgpu
{
    struct cmd_buffer
    {
                                        cmd_buffer                  ( void )                                noexcept = default;
        XGPU_INLINE                     cmd_buffer                  ( cmd_buffer&& CmdBuffer )              noexcept;
        XGPU_INLINE                    ~cmd_buffer                  ( void )                                noexcept;
        XGPU_INLINE     cmd_buffer&     setPipelineInstance         ( xgpu::pipeline_instance& Instance, std::span<xgpu::buffer*> StaticBuffers = {})   noexcept;

        XGPU_INLINE     cmd_buffer&     setDynamicUBO               ( xgpu::buffer& Buffer, int BindIndex) noexcept;

        XGPU_INLINE     cmd_buffer&     setStreamingBuffers         ( std::span<xgpu::buffer> Buffers, int StartingElementIndex=0) noexcept;
        // Buffers not next to each other: an optional index buffer first, then stream 0, 1, ... Each stream's stride is its buffer's entry
        // size, or 0 when its bit (stream i: bit i) is in ConstantStreams: every vertex reads its first element. For a pipeline whose
        // vertex descriptor has m_bDynamicStride (Vulkan 1.3); binding such a pipeline later re-applies these streams.
        XGPU_INLINE     cmd_buffer&     setStreamingBuffers         ( std::span<xgpu::buffer* const> Buffers, std::uint32_t ConstantStreams, int StartingElementIndex=0) noexcept;
        XGPU_INLINE     cmd_buffer&     setBuffer                   ( xgpu::buffer& Buffer, int StartingElementIndex=0) noexcept;
        XGPU_INLINE     cmd_buffer&     setViewport                 ( float x, float y, float w, float h, float minDepth = 0.0f, float maxDepth = 1.0f) noexcept;
        XGPU_INLINE     cmd_buffer&     setScissor                  ( int x, int y, int w, int h )          noexcept;
        template<typename T>
        XGPU_INLINE     cmd_buffer&     setPushConstants            ( const T& PushConstants ) noexcept;
        XGPU_INLINE     cmd_buffer&     setPushConstants            ( const void* pData, std::size_t Size ) noexcept;
        XGPU_INLINE     cmd_buffer&     Draw                        ( int IndexCount, int FirstIndex = 0, int VertexOffset=0 ) noexcept;
        XGPU_INLINE     cmd_buffer&     DrawInstance                ( int InstanceCount, int IndexCount, int FirstInstance=0, int FirstIndex=0, int VertexOffset=0 ) noexcept;
        // Count indexed draws in one call, their commands in a storage buffer from FirstCommand: 5 words each (index count, instance count,
        // first index, vertex offset, first instance: VkDrawIndexedIndirectCommand); the vertex shader tells them apart by gl_InstanceIndex
        XGPU_INLINE     cmd_buffer&     DrawIndexedIndirect         ( xgpu::buffer& Commands, int FirstCommand, int Count ) noexcept;
        XGPU_INLINE     cmd_buffer&     setDepthBias                ( float ConstantFactor, float DepthBiasClamp, float DepthBiasSlope) noexcept;
        XGPU_INLINE     cmd_buffer&     WriteTimestamp              ( xgpu::timestamps& Timestamps, int Index ) noexcept;   // see xgpu_timestamps.h
        XGPU_INLINE     void            Release                     ( void )                                noexcept;

        details::window_handle*         m_pWindow { nullptr };

        std::array<std::byte, 328>      m_Memory;
    };
}