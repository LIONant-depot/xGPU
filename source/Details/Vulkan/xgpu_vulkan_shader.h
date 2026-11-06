namespace xgpu::vulkan
{
    inline std::uint64_t HashMix( std::uint64_t h, std::uint64_t v ) noexcept
    {
        h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        return h;
    }

    inline std::uint64_t HashBytes( std::uint64_t h, const void* p, std::size_t n ) noexcept
    {
        const auto* b = static_cast<const unsigned char*>(p);
        for ( std::size_t i = 0; i < n; ++i )
        {
            h ^= b[i];
            h *= 1099511628211ull;
        }
        return h;
    }

    struct shader : xgpu::details::shader_handle
    {
        xgpu::device::error* Initialize ( std::shared_ptr<vulkan::device>&& Device
                                        , const xgpu::shader::setup&        Setup
                                        ) noexcept;

        std::shared_ptr<vulkan::device> m_Device                {};
        VkShaderModule                  m_VKShaderModule        {};
        std::uint64_t                   m_BytecodeHash          {};     // SPIR-V + entry: equivalent shaders share a compiled VkPipeline
        VkPipelineShaderStageCreateInfo m_ShaderStageCreateInfo {};
        std::atomic_int                 m_nPipelines            { 0 };
    };
}