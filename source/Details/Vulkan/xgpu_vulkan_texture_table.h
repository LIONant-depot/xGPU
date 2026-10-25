namespace xgpu::vulkan
{
    // xgpu::texture_table: its slots and the textures they keep alive; a non-bindless device also builds them into m_VKSet (a combined
    // image sampler per slot, from the device's table pools)
    struct texture_table final : xgpu::details::texture_table_handle
    {
        xgpu::texture_table::error*     Initialize      ( std::shared_ptr<vulkan::device>&& Device, const xgpu::texture_table::setup& Setup ) noexcept;
        virtual                        ~texture_table   ( void ) noexcept override;

        virtual xgpu::texture_table::error* Update      ( int iSlot, const xgpu::texture_table::slot& Slot ) noexcept override;
        virtual int                     getSlotCount    ( void ) const noexcept override { return static_cast<int>(m_Slots.size()); }
        virtual xgpu::texture_table::slot getSlot       ( int iSlot ) const noexcept override { return m_Slots[iSlot]; }
        virtual void                    DeathMarch      ( xgpu::texture_table&& Table ) noexcept override;

        xgpu::texture_table::error*     BuildSet        ( void ) noexcept;      // non-bindless: m_VKSet from m_Slots (the old one queued)

        std::shared_ptr<vulkan::device>                 m_Device    {};
        std::vector<xgpu::texture_table::slot>          m_Slots     {};
        std::vector<std::shared_ptr<vulkan::texture>>   m_Textures  {};         // per slot (null: the white texture)
        VkDescriptorSet                                 m_VKSet     {};         // non-bindless only
        VkDescriptorPool                                m_VKPool    {};         // the pool m_VKSet came from
    };
}
