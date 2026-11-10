namespace xgpu::vulkan
{
    struct buffer final : xgpu::details::buffer_handle
    {
        virtual                        ~buffer                  ( void )                                                                noexcept override;
                xgpu::device::error*    Initialize              ( std::shared_ptr<device>&& Device, const xgpu::buffer::setup& Setup )  noexcept;
        virtual xgpu::device::error*    MapLock                 ( void*& pMemory, int StartIndex, int Count, bool bWaitInFlight = true ) noexcept override;
        virtual xgpu::device::error*    MapUnlock               ( int StartIndex, int Count )                                           noexcept override;
        virtual void                    WaitFramesUpTo          ( std::int64_t Frame )                                                  noexcept override;
        virtual int                     getEntryCount           ( void ) const                                                          noexcept override;
        virtual xgpu::buffer::error*    Resize                  ( int NewEntryCount )                                                   noexcept override;
                xgpu::device::error*    TransferToDestination   ( void )                                                                noexcept;
                xgpu::device::error*    Create                  ( const xgpu::buffer::setup& Setup )                                    noexcept;
                void                    Destroy                 ( void )                                                                noexcept;
        virtual void*                   allocEntry              ( void )                                                                noexcept override;
        virtual int                     getEntrySize            ( void )                                                          const noexcept override;
                
        virtual void                    DeathMarch              ( xgpu::buffer&& buffer )                                               noexcept override;

                int                     getSizeBytes            () { return m_ByteSize; }
                // A storage descriptor's range: the whole buffer, or the device's maxStorageBufferRange when only the allocation's alignment padding goes past it (Create refuses data past it)
                VkDeviceSize            StorageRange            () const noexcept;

        std::shared_ptr<device>     m_Device            {};
        bool                        m_bQueued           = false;    // handed to the device's deferred destroy (DeathMarch): the next last release frees it
        std::atomic<int>            m_CurrentOffset     {};     // For uniform buffers
        std::byte*                  m_pVMapMemory       {};
        // The regions the CPU wrote through a frame-region map and the device frame that wrote each (the frame that then read it): a new
        // write of an overlapping region waits for that frame only (C25: a buffer rewritten at the same offset every frame; a ring's
        // slots never overlap the frame in flight's). Oldest first, a few kept
        struct written { int m_First = 0, m_End = 0; std::int64_t m_Frame = -1; };
        std::array<written, 64>     m_Written           {};
        std::int64_t                m_DroppedFrame      = -1;       // (C30) the regions dropped from m_Written (more than it holds a frame): their span and newest frame - a write over the span waits for it
        int                         m_DroppedFirst      = 0, m_DroppedEnd = 0;
        int                         m_nWritten          {0};
        VkBuffer                    m_VKBuffer          {};
        VkDeviceMemory              m_VKBufferMemory    {};     // host-visible (a GPU_READ buffer's until its data is copied over)
        const char*                 m_pName             = nullptr;  // setup::m_pName
        vram_allocation             m_VRAM              {};     // a GPU_READ buffer's device-local range (device::AllocVRAM)
        int                         m_LastOffset        {};
        int                         m_AllocFrame        {-1};   // dynamic UBO: the device frame of its last entry, where that frame and the one
        int                         m_FrameStart        {-1};   //   before it started in the ring: coming back to the frame before's first entry
        int                         m_PrevFrameStart    {-1};   //   (the frame in flight's) waits for it
        int                         m_ByteSize          {};
        int                         m_nEntries          {};
        int                         m_EntrySizeBytes    {};
        xgpu::buffer::type          m_Type              {};
        xgpu::buffer::setup::usage  m_Usage             {};
        bool                        m_bWriteOnly        {};
    };
}
