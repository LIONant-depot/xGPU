namespace xgpu::vulkan
{
    struct device;

    // The streaming lane: textures and buffers created with data are copied on the transfer queue from a fixed staging ring (it never grows;
    // an upload larger than half of it gets a staging buffer of its own, freed with its batch), in batches that signal a timeline
    // semaphore. Nothing waits for the main queue to go idle: every main-queue submission (window::EndFrame, the setup command buffer)
    // waits on the GPU for the last flushed batch (TakeAcquires). When the transfer queue is of another family, a batch releases what it
    // wrote and its acquire command buffer (main family, recorded with it) takes it over at the front of the next main-queue submission,
    // which signals m_AcquireTimeline so the batch is reused only after that ran. A device with one queue family records the same in one
    // command buffer and needs no acquire.
    struct upload_queue
    {
        static constexpr int batches_v = 4;

        struct batch
        {
            VkCommandBuffer                                     m_Transfer      {};     // transfer family
            VkCommandBuffer                                     m_Acquire       {};     // main family (own transfer family only)
            std::uint64_t                                       m_Value         = 0;    // the timeline value it signals (0: free)
            std::uint64_t                                       m_AcquireValue  = 0;    // m_AcquireTimeline's value of the submission that ran m_Acquire (0: not yet)
            std::uint64_t                                       m_RingEnd       = 0;    // the ring's head at its flush: the tail moves there when it retires
            int                                                 m_nCommands     = 0;
            bool                                                m_bOpen         = false;
            bool                                                m_bAcquires     = false;
            std::vector<std::pair<VkBuffer, VkDeviceMemory>>    m_Free;                 // staging freed when it retires
        };

        struct staging
        {
            std::byte*      m_pData     = nullptr;
            VkBuffer        m_Buffer    = VK_NULL_HANDLE;
            VkDeviceSize    m_Offset    = 0;
        };

        xgpu::device::error*    Init            ( device& Device ) noexcept;
        // A texture's mips (the regions point into Data) into its image; it ends in SHADER_READ_ONLY_OPTIMAL, owned by the main family
        xgpu::device::error*    UploadImage     ( VkImage Image, VkImageSubresourceRange Range, std::span<const std::byte> Data, std::span<VkBufferImageCopy> Regions ) noexcept;
        // Bytes of a host staging buffer into a device-local one (the staging buffer is freed when the batch retires)
        xgpu::device::error*    UploadBuffer    ( VkBuffer Staging, VkDeviceMemory StagingMemory, VkBuffer Dst, VkDeviceSize Bytes ) noexcept;
        std::uint64_t           Flush           ( void ) noexcept;                     // the timeline value the main queue waits on
        // For a main-queue submission: the batches' acquires to run first, the timeline value to wait on, the acquire timeline value to
        // signal (0: no acquire)
        void                    TakeAcquires    ( std::vector<VkCommandBuffer>& CmdBuffers, std::uint64_t& WaitValue, std::uint64_t& AcquireSignal ) noexcept;
        bool                    isDone          ( std::uint64_t Value ) noexcept;
        void                    Wait            ( std::uint64_t Value ) noexcept;
        void                    Destroy         ( void ) noexcept;

        // (m_Lock held)
        batch*                  Open            ( void ) noexcept;
        void                    Retire          ( void ) noexcept;
        std::uint64_t           FlushLocked     ( void ) noexcept;
        xgpu::device::error*    Reserve         ( std::size_t Bytes, staging& Out ) noexcept;
        void                    SubmitAcquiresAlone( void ) noexcept;                   // nothing on the main queue took a finished batch's acquires

        device*                 m_pDevice           = nullptr;
        std::mutex              m_Lock;
        bool                    m_bOwnFamily        = false;
        VkSemaphore             m_Timeline          {};
        VkSemaphore             m_AcquireTimeline   {};
        std::uint64_t           m_Flushed           = 0;        // the last value a batch signals
        std::uint64_t           m_Acquired          = 0;        // the last m_AcquireTimeline value handed out
        VkCommandPool           m_TransferPool      {};
        VkCommandPool           m_AcquirePool       {};
        VkBuffer                m_Ring              {};
        VkDeviceMemory          m_RingMemory        {};
        std::byte*              m_pRing             = nullptr;
        std::uint64_t           m_RingSize          = 0;
        std::uint64_t           m_Head              = 0;        // monotonic bytes written / released
        std::uint64_t           m_Tail              = 0;
        std::array<batch, batches_v> m_Batches      {};
        int                     m_iOpen             = -1;
        std::vector<int>        m_Pending;                      // flushed batches whose acquires no main submission has taken yet
        std::deque<int>         m_InFlight;                     // flushed batches, oldest first
        std::uint64_t           m_nUploads          = 0;        // diagnostics: uploads recorded, ring wraps, own staging buffers
        std::uint64_t           m_nWraps            = 0;
        std::uint64_t           m_nOversized        = 0;
    };
}
