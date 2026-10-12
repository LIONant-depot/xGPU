namespace xgpu::vulkan
{
    // One VkQueryPool holding ring_size_v query sets of m_Count timestamps. The set in use changes when the device's
    // frame index changes (every PageFlip); finished sets are read with VK_QUERY_RESULT_WITH_AVAILABILITY_BIT (never
    // waits) and recycled with a host reset.
    struct timestamps final : xgpu::details::timestamps_handle
    {
        constexpr static int ring_size_v = 4;       // must exceed the frames in flight (xGPU keeps 1 today: EndFrame waits)

        struct set
        {
            int     m_Frame     { -1 };             // device frame index that wrote this set, -1 = free
        };

                xgpu::device::error*    Initialize  ( std::shared_ptr<device>&& Device, const xgpu::timestamps::setup& Setup ) noexcept;
        virtual                        ~timestamps  ( void )                                    noexcept override;
                void                    Write       ( VkCommandBuffer CmdBuffer, int Index )    noexcept;
        virtual bool                    getResults  ( std::vector<std::uint64_t>& NanoSeconds ) noexcept override;
        virtual int                     getCount    ( void ) const                              noexcept override { return m_Count; }

        std::shared_ptr<device>             m_Device        {};
        VkQueryPool                         m_VKQueryPool   {};
        int                                 m_Count         {};
        int                                 m_iCurrent      {};                     // set the current frame writes to
        double                              m_NsPerTick     {};
        std::uint64_t                       m_TickMask      {};
        std::array<set, ring_size_v>        m_Sets          {};
        std::vector<std::uint8_t>           m_Written       {};                     // ring_size_v * m_Count flags
    };
}
