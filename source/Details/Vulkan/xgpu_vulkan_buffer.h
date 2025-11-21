namespace xgpu::vulkan
{
    struct buffer final : xgpu::details::buffer_handle
    {
        virtual                        ~buffer                  ( void )                                                                noexcept override;
                xgpu::device::error*    Initialize              ( std::shared_ptr<device>&& Device, const xgpu::buffer::setup& Setup )  noexcept;
        virtual xgpu::device::error*    MapLock                 ( void*& pMemory, int StartIndex, int Count )                           noexcept override;
        virtual xgpu::device::error*    MapUnlock               ( int StartIndex, int Count )                                           noexcept override;
        virtual int                     getEntryCount           ( void ) const                                                          noexcept override;
        virtual xgpu::buffer::error*    Resize                  ( int NewEntryCount )                                                   noexcept override;
                xgpu::device::error*    TransferToDestination   ( void )                                                                noexcept;
                xgpu::device::error*    Create                  ( const xgpu::buffer::setup& Setup )                                    noexcept;
                void                    Destroy                 ( void )                                                                noexcept;
        virtual void*                   allocEntry              ( void )                                                                noexcept override;
        virtual int                     getEntrySize            ( void )                                                          const noexcept override;
                
        virtual void                    DeathMarch              ( xgpu::buffer&& buffer )                                               noexcept override;

                int                     getSizeBytes            () { return m_ByteSize; }

        std::shared_ptr<device>     m_Device            {};
        std::atomic<int>            m_CurrentOffset     {};     // For uniform buffers
        std::byte*                  m_pVMapMemory       {};
        VkBuffer                    m_VKBuffer          {};
        VkDeviceMemory              m_VKBufferMemory    {};
        int                         m_LastOffset        {};
        int                         m_ByteSize          {};
        int                         m_nEntries          {};
        int                         m_EntrySizeBytes    {};
        xgpu::buffer::type          m_Type              {};
        xgpu::buffer::setup::usage  m_Usage             {};
    };
}
