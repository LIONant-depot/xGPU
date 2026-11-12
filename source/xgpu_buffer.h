namespace xgpu
{
    struct buffer
    {
        enum class error : std::uint8_t
        {
            FAILURE
        };

        enum class type : std::uint8_t
        { VERTEX            // Primitive Vertex array
        , INDEX             // Primitive Index array
        , UNIFORM           // Maximum of 64K
        , STORAGE           // Read only or read/write... unlimited size
        };

        // One region of window::CopyBuffer, in entries (both buffers must have the same entry size)
        struct copy
        {
            int     m_SrcEntry  = 0;
            int     m_DstEntry  = 0;
            int     m_Count     = 1;
        };

        struct setup
        {
            enum class usage : std::uint8_t
            {
                GPU_READ
            ,   CPU_WRITE_GPU_READ
            };

            type                 m_Type          {};
            usage                m_Usage         { usage::GPU_READ };
            int                  m_EntryByteSize {-1};
            int                  m_EntryCount    {-1};
            std::optional<void*> m_pData         {};
            const char*          m_pName         { nullptr };  // what it is - "Category: detail", a literal (the profiler's memory map; null: buffer)
            bool                 m_bWriteOnly    { false };     // (CPU_WRITE_GPU_READ the CPU writes, reads rarely - uncached, slow: device-local memory
                                                                //  it can map where the device has it - resizable BAR - for the GPU's reads; else as before)
        };

        template< typename T_CALLBACK >
        [[nodiscard]]
        error*              MemoryMap               ( int StartIndex, int Count, T_CALLBACK&& Callback ) noexcept;

        // MemoryMap without waiting for the frame in flight: for a region no submitted frame reads (this frame's slot of a ring that has a
        // slot per frame in flight and one more). MemoryMap waits for that frame first: it may still read what is about to be written.
        // xGPU waits only for the frame that last WROTE an overlapping region (data written and read by the same frame); it does not know
        // which frames read a region. Data written once and read by the frames after it (a matrix written when it changes) is not a frame
        // region: MemoryMap, or MemoryMapAfterFrame with the last frame that read it (C34; XGPU_REUSE_CHECK reports such a write)
        template< typename T_CALLBACK >
        [[nodiscard]]
        error*              MemoryMapFrameRegion    ( int StartIndex, int Count, T_CALLBACK&& Callback ) noexcept;

        // MemoryMap after the device frame Frame is done (only that frame's submission and the ones before it waited): for a region the
        // caller knows was last read by that frame (a set of buffers used in turn: write each after the frame that last used it)
        template< typename T_CALLBACK >
        [[nodiscard]]
        error*              MemoryMapAfterFrame     ( std::int64_t Frame, int StartIndex, int Count, T_CALLBACK&& Callback ) noexcept;

        [[nodiscard]] XGPU_INLINE
        
        int                 getEntryCount           ( void ) const noexcept;

        template< typename T >
        [[nodiscard]]
        T&                  allocEntry              ( void ) noexcept;


        [[nodiscard]] XGPU_INLINE
        
        error*              Resize                  ( int NewEntryCount ) noexcept;

        template< typename T >
        [[nodiscard]]
        error*              getMappedVirtualMemory  ( T*& Pointer ) noexcept;

                            buffer                  ( void ) noexcept = default;
                            buffer                  ( const buffer& ) noexcept = default;
                            buffer                  ( buffer&& ) noexcept = default;
        // Assigning over a handle lets go of the old buffer as the destructor does - through the deferred destroy (B = {} freed a buffer
        // a recorded upload still used, at once)
        XGPU_INLINE
        buffer&             operator=               ( const buffer& Other ) noexcept;
        XGPU_INLINE
        buffer&             operator=               ( buffer&& Other ) noexcept;

        XGPU_INLINE
                           ~buffer                  ( void ) noexcept;

        XGPU_INLINE
        void                Release                 ( void ) noexcept;      // the last handle: into the device's deferred destroy

        std::shared_ptr<details::buffer_handle> m_Private{};
    };
}
