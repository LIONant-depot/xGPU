namespace xgpu::vulkan
{
    struct texture final : xgpu::details::texture_handle
    {
        xgpu::device::error* Initialize( std::shared_ptr<vulkan::device>&& Device
                                       , const xgpu::texture::setup&       Setup
                                       ) noexcept;

        virtual ~texture( void ) noexcept override;

        int getMipCount( void ) noexcept
        {
            return 0;
        }

        std::shared_ptr<vulkan::device> m_Device                {};
        std::uint32_t                   m_nMips                 {};
        VkImage                         m_VKImage               {};
        VkImageView                     m_VKView                {};
        VkDeviceMemory                  m_VKDeviceMemory        {};
    };
}
