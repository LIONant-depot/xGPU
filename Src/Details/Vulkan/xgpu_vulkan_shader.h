namespace xgpu::vulkan
{
    struct shader : xgpu::details::shader_handle
    {
        xgpu::device::error* Initialize ( std::shared_ptr<vulkan::device>&& Device
                                        , const xgpu::shader::setup&        Setup
                                        ) noexcept;

        using push_constant_array = std::array<VkPushConstantRange, 1>;

        std::shared_ptr<vulkan::device> m_Device                {};
        VkShaderModule                  m_VKShaderModule        {};
        VkPipelineShaderStageCreateInfo m_ShaderStageCreateInfo {};
        int                             m_nPushConstantRanges   {};
        push_constant_array             m_VKPushConstantRanges  {};
        std::atomic_int                 m_nPipelines            { 0 };
    };
}