namespace xgpu::vulkan
{
    struct shader : xgpu::details::shader_handle
    {
        xgpu::device::error* Initialize ( std::shared_ptr<vulkan::device>&& Device
                                        , const xgpu::shader::setup&        Setup
                                        ) noexcept;

        std::shared_ptr<vulkan::device> m_Device                {};
        VkShaderModule                  m_VKShaderModule        {};
        VkPipelineShaderStageCreateInfo m_ShaderStageCreateInfo {};
        std::atomic_int                 m_nPipelines            { 0 };
    };
}