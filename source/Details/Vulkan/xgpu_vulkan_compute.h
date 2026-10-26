namespace xgpu::vulkan
{
    // xgpu::compute: one descriptor set layout of m_nBuffers storage buffers (set 0), a push-constant range, a compute pipeline.
    // window::Dispatch takes a descriptor set for it from the frame's ring (reset when the frame's fence has passed)
    struct compute final : xgpu::details::compute_handle
    {
        static constexpr int        max_buffers_v       = 16;          // storage buffers a compute shader binds (set 0, bindings 0..)
                xgpu::device::error*    Initialize  ( std::shared_ptr<device>&& Device, const xgpu::compute::setup& Setup ) noexcept;
        virtual                        ~compute     ( void ) noexcept override;

        std::shared_ptr<device>     m_Device            {};
        static constexpr int        max_textures_v      = 8;           // sampled textures (set 1, bindings 0..)
        VkDescriptorSetLayout       m_VKSetLayout       {};
        VkDescriptorSetLayout       m_VKTextureLayout   {};            // (m_nTextures > 0)
        VkSampler                   m_VKSampler         {};            //   its textures' sampler: nearest, clamped (texelFetch reads)
        VkPipelineLayout            m_VKPipelineLayout  {};
        VkPipeline                  m_VKPipeline        {};
        int                         m_nBuffers          {};
        int                         m_nTextures         {};
        std::uint32_t               m_PushSize          {};
    };
}
