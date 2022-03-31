namespace xgpu
{
    struct buffer
    {
        enum class error : std::uint8_t
        {
            FAILURE
        };

        enum class type : std::uint8_t
        {
            VERTEX
        ,   INDEX
        ,   UNIFORM
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

        XGPU_INLINE
        [[nodiscard]] 
        int                 getEntryCount           ( void ) const noexcept;

        XGPU_INLINE
        [[nodiscard]] 
        error*              Resize                  ( int NewEntryCount ) noexcept;

        template< typename T >
        [[nodiscard]]
        error*              getMappedVirtualMemory  ( T*& Pointer ) noexcept;

        std::shared_ptr<details::buffer_handle> m_Private{};
    };
}
