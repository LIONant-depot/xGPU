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
        };

        template< typename T_CALLBACK >
        [[nodiscard]]
        error*              MemoryMap               ( int StartIndex, int Count, T_CALLBACK&& Callback ) noexcept;

        // MemoryMap without waiting for the frame in flight: for a region no submitted frame reads (this frame's slot of a ring that has a
        // slot per frame in flight and one more). MemoryMap waits for that frame first: it may still read what is about to be written
        template< typename T_CALLBACK >
        [[nodiscard]]
        error*              MemoryMapFrameRegion    ( int StartIndex, int Count, T_CALLBACK&& Callback ) noexcept;

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

        XGPU_INLINE
                           ~buffer                  ( void ) noexcept;

        std::shared_ptr<details::buffer_handle> m_Private{};
    };
}
