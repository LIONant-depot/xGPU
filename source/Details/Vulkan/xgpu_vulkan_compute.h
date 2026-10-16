namespace xgpu::vulkan
{
    // xgpu::compute: one descriptor set layout of m_nBuffers storage buffers (set 0), a push-constant range, a compute pipeline.
    // window::Dispatch takes a descriptor set for it from the frame's ring (reset when the frame's fence has passed)
    struct compute final : xgpu::details::compute_handle
    {
                xgpu::device::error*    Initialize  ( std::shared_ptr<device>&& Device, const xgpu::compute::setup& Setup ) noexcept;
        virtual                        ~compute     ( void ) noexcept override;

        std::shared_ptr<device>     m_Device            {};
        VkDescriptorSetLayout       m_VKSetLayout       {};
        VkPipelineLayout            m_VKPipelineLayout  {};
        VkPipeline                  m_VKPipeline        {};
        int                         m_nBuffers          {};
        std::uint32_t               m_PushSize          {};
    };
}
