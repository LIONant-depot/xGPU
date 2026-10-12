namespace xgpu
{
    // GPU timestamps for measuring passes/frames without stalling the CPU.
    // Write slots with cmd_buffer::WriteTimestamp(Timestamps, Index) while recording a frame (inside or outside a
    // render pass), then call getResults() any time later: it returns the newest frame whose GPU work is done and
    // never waits. Each index may be written once per frame. Internally a small ring of query sets is used, so it
    // stays correct when xGPU keeps several frames in flight.
    struct timestamps
    {
        struct setup
        {
            int     m_Count     { 64 };     // slots per frame
        };

        // Fills NanoSeconds (resized to getCount()) with the GPU time of every slot of the newest finished frame.
        // Slots not written that frame are ~0ull. Values are absolute GPU ticks converted to ns: subtract two
        // slots to get a duration. Returns false (and leaves NanoSeconds untouched) when no new frame is ready.
        XGPU_INLINE bool    getResults  ( std::vector<std::uint64_t>& NanoSeconds ) noexcept;
        XGPU_INLINE int     getCount    ( void ) const noexcept;

        std::shared_ptr<details::timestamps_handle> m_Private;
    };
}
