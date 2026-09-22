namespace xgpu::vulkan
{
    struct texture final : xgpu::details::texture_handle
    {
        xgpu::device::error* Initialize( std::shared_ptr<vulkan::device>&& Device
                                       , const xgpu::texture::setup&       Setup
                                       ) noexcept;

        // Writes Source into mip 0's [OffsetX,OffsetY]..[+Width,+Height] sub-rectangle - same staging-buffer +
        // vkCmdCopyBufferToImage idiom Initialize's own staging path already uses, just a smaller region
        // instead of the whole image, and reusing the already-created image instead of making a new one.
        xgpu::device::error* UpdateRegion( int OffsetX, int OffsetY, int Width, int Height, std::span<const std::byte> Source ) noexcept;

        // Reads mip 0 back as packed B8G8R8A8 pixels - same transition+copy+transition-back idiom
        // xgpu_vulkan_window.cpp's CaptureBackbuffer uses for the swapchain, generalized to this texture's
        // own VkImage/SHADER_READ_ONLY_OPTIMAL resting layout instead of the backbuffer/PRESENT_SRC_KHR.
        xgpu::device::error* Readback( std::vector<std::uint32_t>& Dest, int& Width, int& Height ) noexcept;

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

        // Whether the image actually rests at SHADER_READ_ONLY_OPTIMAL right now (true once Initialize's own
        // "with data" staging path has run, or after any UpdateRegion) - the "empty" (m_nMips==0, used for
        // render targets) Initialize path leaves the real image at UNDEFINED despite m_VKDescriptorImageInfo
        // claiming SHADER_READ_ONLY_OPTIMAL (fine for that path's actual use - a render pass's own
        // finalLayout does the real transition later), but UpdateRegion's own barrier needs to know which of
        // the two is actually true for ITS "from" layout, since it runs outside any render pass.
        bool                            m_bContentReady         {};
    };
}
