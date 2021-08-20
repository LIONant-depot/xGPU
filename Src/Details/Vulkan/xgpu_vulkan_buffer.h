namespace xgpu::vulkan
{
    struct buffer final : xgpu::details::buffer_handle
    {
                xgpu::device::error*    Initialize              ( std::shared_ptr<device>&& Device, const xgpu::buffer::setup& Setup )  noexcept;
        virtual xgpu::device::error*    MapLock                 ( void*& pMemory, int StartIndex, int Count )                           noexcept override;
        virtual xgpu::device::error*    MapUnlock               ( int StartIndex, int Count )                                           noexcept override;
        virtual int                     getEntryCount           ( void ) const                                                          noexcept override;
                xgpu::device::error*    TransferToDestination   ( void )                                                                noexcept;


        std::shared_ptr<device>     m_Device            {};
        VkBuffer                    m_VKBuffer          {};
        VkDeviceMemory              m_VKBufferMemory    {};
        int                         m_ByteSize          {};
        int                         m_nEntries          {};
        int                         m_EntrySizeBytes    {};
        xgpu::buffer::type          m_Type              {};
        xgpu::buffer::setup::usage  m_Usage             {};
    };
}
