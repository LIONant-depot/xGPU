namespace xgpu
{
    namespace details
    {
        struct buffer_handle
        {
            virtual                                    ~buffer_handle   ( void )                                        noexcept = default;
            virtual xgpu::device::error*                MapLock         ( void*& pMemory, int StartIndex, int Count )   noexcept = 0;
            virtual xgpu::device::error*                MapUnlock       ( int StartIndex, int Count )                   noexcept = 0;
            virtual int                                 getEntryCount   ( void )                                  const noexcept = 0;
            virtual xgpu::buffer::error*                Resize          ( int NewEntryCount )                           noexcept = 0;
        };
    }

    //--------------------------------------------------------------------------------------------------------

    template< typename T_CALLBACK >
    xgpu::buffer::error* buffer::MemoryMap(int StartIndex, int Count, T_CALLBACK&& Callback) noexcept
    {
        void* pMemory = nullptr;
        if( auto pErr = m_Private->MapLock(pMemory, StartIndex, Count ); pErr ) return reinterpret_cast<xgpu::buffer::error*>(pErr);
        Callback(pMemory);
        if (auto pErr = m_Private->MapUnlock( StartIndex, Count ); pErr) return reinterpret_cast<xgpu::buffer::error*>(pErr);
        return nullptr;
    }

    //--------------------------------------------------------------------------------------------------------

    template< typename T >
    xgpu::buffer::error* buffer::getMappedVirtualMemory( T*& Pointer ) noexcept
    {
        void* pMemory = nullptr;
        if (auto pErr = m_Private->MapLock(pMemory, 0, 1); pErr) return reinterpret_cast<xgpu::buffer::error*>(pErr);
        Pointer = reinterpret_cast<T*>(pMemory);
        return nullptr;
    }

    //--------------------------------------------------------------------------------------------------------

    int buffer::getEntryCount(void) const noexcept
    {
        return m_Private->getEntryCount();
    }

    //--------------------------------------------------------------------------------------------------------

    [[nodiscard]] xgpu::buffer::error* 
    buffer::Resize(int NewEntryCount) noexcept
    {
        return m_Private->Resize( NewEntryCount );
    }

}