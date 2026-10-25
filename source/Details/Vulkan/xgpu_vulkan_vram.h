namespace xgpu::vulkan
{
    // A device-local range (device::AllocVRAM): a range of one of the device's blocks (64 MB, one memory type, images and buffers in blocks
    // of their own so bufferImageGranularity never applies), or a whole allocation of its own when it is large (m_iBlock < 0)
    struct vram_allocation
    {
        VkDeviceMemory                  m_Memory    {};
        VkDeviceSize                    m_Offset    = 0;
        int                             m_iBlock    = -1;
        OffsetAllocator::Allocation     m_Range     {};
        bool                            isValid     ( void ) const noexcept { return m_Memory != VK_NULL_HANDLE; }
    };

    struct vram_block
    {
        VkDeviceMemory                                  m_Memory    {};
        std::uint32_t                                   m_Type      = 0;
        bool                                            m_bImage    = false;
        std::unique_ptr<OffsetAllocator::Allocator>     m_pAlloc;           // in vram_unit_v units
        int                                             m_nLive     = 0;
    };

    inline static constexpr VkDeviceSize vram_block_v     = VkDeviceSize(64) << 20;
    inline static constexpr VkDeviceSize vram_unit_v      = 256;
    inline static constexpr VkDeviceSize vram_dedicated_v = VkDeviceSize(16) << 20;    // larger: an allocation of its own
}
