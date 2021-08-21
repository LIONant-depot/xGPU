namespace xgpu
{
    struct cmd_buffer
    {
        VGPU_INLINE                     cmd_buffer                  ( details::window_handle* )             noexcept;
        VGPU_INLINE                     cmd_buffer                  ( cmd_buffer&& )                        noexcept;
        VGPU_INLINE                    ~cmd_buffer                  ( void )                                noexcept;
        VGPU_INLINE     cmd_buffer&     setPipelineInstance         ( xgpu::pipeline_instance& Instance )   noexcept;
        VGPU_INLINE     cmd_buffer&     setBuffer                   ( xgpu::buffer& Buffer, int StartingElementIndex=0) noexcept;
        VGPU_INLINE     cmd_buffer&     setViewport                 ( float x, float y, float w, float h, float minDepth = 0.0f, float maxDepth = 1.0f) noexcept;
        VGPU_INLINE     cmd_buffer&     setScissor                  ( int x, int y, int w, int h )          noexcept;
        VGPU_INLINE     cmd_buffer&     setConstants                ( xgpu::shader::type Type, int Offset, const void* pData, int Size ) noexcept;
        VGPU_INLINE     cmd_buffer&     Draw                        ( int IndexCount, int FirstIndex = 0, int VertexOffset=0 ) noexcept;
        VGPU_INLINE     cmd_buffer&     DrawInstance                ( int InstanceCount, int IndexCount, int FirstInstance=0, int FirstIndex=0, int VertexOffset=0 ) noexcept;

        VGPU_INLINE     void            Release                     ( void )                                noexcept;

        details::window_handle* m_pWindow { nullptr };
    };
}