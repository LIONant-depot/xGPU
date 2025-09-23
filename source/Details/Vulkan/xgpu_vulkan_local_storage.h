namespace xgpu::vulkan
{
    struct local_storage
    {
        struct per_device
        {
            xgpu::vulkan::device*           m_pDevice           {nullptr};
            VkCommandPool                   m_VKCmdPool         {};
            VkCommandBuffer                 m_VKSetupCmdBuffer  {};

            xgpu::device::error* FlushVKSetupCommandBuffer ( void ) noexcept;
            xgpu::device::error* CreateVKSetupCommandBuffer( void ) noexcept;
        };

        std::tuple<per_device&, xgpu::device::error*> getOrCreatePerDevice( xgpu::vulkan::device& Device ) noexcept;

        int                         m_nDevices  {0};
        std::array<per_device,8>    m_PerDevice {};
    };
}