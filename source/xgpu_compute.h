namespace xgpu
{
    // A compute shader with its storage buffers (set 0, bindings 0 .. m_nBuffers - 1, at most 8), its textures (set 1, bindings 0 ..
    // m_nTextures - 1: sampler2D, each with its own sampler - a render pass's depth is read where the pass left it) and push constants.
    // Created with device::Create, run with window::Dispatch (recorded in the frame's command buffer between render passes)
    struct compute
    {
        struct setup
        {
            const shader*   m_pShader           { nullptr };    // a shader::type::bit::COMPUTE shader
            int             m_nBuffers          { 1 };
            int             m_nTextures         { 0 };
            std::size_t     m_PushConstantsSize { 0 };          // bytes (at most 128: the minimum every device guarantees)
        };

        std::shared_ptr<details::compute_handle> m_Private;
    };
}
