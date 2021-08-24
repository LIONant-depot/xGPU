namespace xgpu
{
    struct cmd_buffer
    {
        XGPU_INLINE                     cmd_buffer                  ( details::window_handle* )             noexcept;
        XGPU_INLINE                     cmd_buffer                  ( cmd_buffer&& )                        noexcept;
        XGPU_INLINE                    ~cmd_buffer                  ( void )                                noexcept;
        XGPU_INLINE     cmd_buffer&     setPipelineInstance         ( xgpu::pipeline_instance& Instance )   noexcept;
        XGPU_INLINE     cmd_buffer&     setBuffer                   ( xgpu::buffer& Buffer, int StartingElementIndex=0) noexcept;
        XGPU_INLINE     cmd_buffer&     setViewport                 ( float x, float y, float w, float h, float minDepth = 0.0f, float maxDepth = 1.0f) noexcept;
        XGPU_INLINE     cmd_buffer&     setScissor                  ( int x, int y, int w, int h )          noexcept;
        XGPU_INLINE     cmd_buffer&     setConstants                ( xgpu::shader::type Type, int Offset, const void* pData, int Size ) noexcept;
        XGPU_INLINE     cmd_buffer&     Draw                        ( int IndexCount, int FirstIndex = 0, int VertexOffset=0 ) noexcept;
        XGPU_INLINE     cmd_buffer&     DrawInstance                ( int InstanceCount, int IndexCount, int FirstInstance=0, int FirstIndex=0, int VertexOffset=0 ) noexcept;

        XGPU_INLINE     void            Release                     ( void )                                noexcept;

        details::window_handle* m_pWindow { nullptr };
    };
}