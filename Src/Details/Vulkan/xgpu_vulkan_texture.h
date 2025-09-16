namespace xgpu::vulkan
{
    struct texture final : xgpu::details::texture_handle
    {
        xgpu::device::error* Initialize( std::shared_ptr<vulkan::device>&& Device
                                       , const xgpu::texture::setup&       Setup
                                       ) noexcept;

        virtual ~texture( void ) noexcept override;

        virtual std::array<int, 3>                  getTextureDimensions(void)                  const   noexcept override;
        virtual int                                 getMipCount         (void)                  const   noexcept override;
        virtual xgpu::texture::format               getFormat           (void)                  const   noexcept override;
        virtual bool                                isCubemap           (void)                  const   noexcept override;
        virtual std::array<xgpu::texture::address_mode, 3> getAdressModes(void)                 const   noexcept override;

        virtual void                                DeathMarch(xgpu::texture&& buffer)                  noexcept override;

        std::shared_ptr<vulkan::device> m_Device                {};
        VkImage                         m_VKImage               {};
        VkImageView                     m_VKView                {};
        VkDeviceMemory                  m_VKDeviceMemory        {};
        VkDescriptorImageInfo           m_VKDescriptorImageInfo {};
        std::uint16_t                   m_Width                 {};
        std::uint16_t                   m_Height                {};
        std::uint16_t                   m_ArrayCount            {};
        std::uint8_t                    m_nMips                 {};
        xgpu::texture::format           m_Format                {};
        VkFormat                        m_VKFormat              {};
        std::array<xgpu::texture::address_mode, 3> m_AdressModes {};
        bool                            m_bCubeMap              {};
    };
}
