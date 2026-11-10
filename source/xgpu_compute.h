namespace xgpu
{
    // A compute shader with its storage buffers (set 0, bindings 0 .. m_nBuffers - 1, at most 8), its textures (set 1, bindings 0 ..
    // m_nTextures - 1: sampler2D, each with its own sampler - a render pass's depth is read where the pass left it), its storage images (set 2,
    // bindings 0 .. m_nImages - 1: image2D, of textures made with texture::setup::m_bStorage; a dispatch writes them, sampled again after it) and push constants.
    // Created with device::Create, run with window::Dispatch (recorded in the frame's command buffer between render passes)
    struct compute
    {
        struct setup
        {
            const shader*   m_pShader           { nullptr };    // a shader::type::bit::COMPUTE shader
            int             m_nBuffers          { 1 };
            int             m_nTextures         { 0 };
            int             m_nImages           { 0 };
            std::size_t     m_PushConstantsSize { 0 };          // bytes (at most 128: the minimum every device guarantees)
            bool            m_bLinear           { false };      // the textures' sampler filters bilinear (textureLod between texels blends; texelFetch is unaffected) - else nearest
            const char*     m_pName             { nullptr };    // (a string that lives as long as it: the frame map's label)
        };

        std::shared_ptr<details::compute_handle> m_Private;
    };
}
