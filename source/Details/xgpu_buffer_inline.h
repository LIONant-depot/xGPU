namespace xgpu
{
    namespace details
    {
        struct buffer_handle
        {
            virtual                                    ~buffer_handle   ( void )                                        noexcept = default;
            virtual xgpu::device::error*                MapLock         ( void*& pMemory, int StartIndex, int Count, bool bWaitInFlight = true ) noexcept = 0;
            virtual xgpu::device::error*                MapUnlock       ( int StartIndex, int Count )                   noexcept = 0;
            virtual int                                 getEntryCount   ( void )                                  const noexcept = 0;
            virtual int                                 getEntrySize    (void)                                    const noexcept = 0;
            virtual xgpu::buffer::error*                Resize          ( int NewEntryCount )                           noexcept = 0;
            virtual void                                DeathMarch      ( xgpu::buffer&& )                              noexcept = 0;
            virtual void*                               allocEntry      (void)                                          noexcept = 0;
            virtual void                                WaitFramesUpTo  ( std::int64_t Frame )                          noexcept = 0;
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

    template< typename T_CALLBACK >
    xgpu::buffer::error* buffer::MemoryMapFrameRegion(int StartIndex, int Count, T_CALLBACK&& Callback) noexcept
    {
        void* pMemory = nullptr;
        if( auto pErr = m_Private->MapLock(pMemory, StartIndex, Count, false ); pErr ) return reinterpret_cast<xgpu::buffer::error*>(pErr);
        Callback(pMemory);
        if (auto pErr = m_Private->MapUnlock( StartIndex, Count ); pErr) return reinterpret_cast<xgpu::buffer::error*>(pErr);
        return nullptr;
    }

    //--------------------------------------------------------------------------------------------------------

    template< typename T_CALLBACK >
    xgpu::buffer::error* buffer::MemoryMapAfterFrame(std::int64_t Frame, int StartIndex, int Count, T_CALLBACK&& Callback) noexcept
    {
        m_Private->WaitFramesUpTo(Frame);
        return MemoryMapFrameRegion(StartIndex, Count, std::forward<T_CALLBACK>(Callback));
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

    template< typename T >
    T& buffer::allocEntry(void) noexcept
    {
        assert(sizeof(T) <= m_Private->getEntrySize());     // uniform entries may be padded to the GPU's offset alignment
        return *std::construct_at(static_cast<T*>(m_Private->allocEntry()));
    }

    //--------------------------------------------------------------------------------------------------------

    [[nodiscard]] xgpu::buffer::error* 
    buffer::Resize(int NewEntryCount) noexcept
    {
        return m_Private->Resize( NewEntryCount );
    }

    //--------------------------------------------------------------------------------------------------------

    void buffer::Release() noexcept
    {
        if (!m_Private) return;
        if (m_Private.use_count() == 1) m_Private->DeathMarch(std::move(*this));
        m_Private.reset();
    }

    buffer& buffer::operator=(const buffer& Other) noexcept
    {
        auto Keep = Other.m_Private;
        Release();
        m_Private = std::move(Keep);
        return *this;
    }

    buffer& buffer::operator=(buffer&& Other) noexcept
    {
        auto Keep = std::move(Other.m_Private);
        Release();
        m_Private = std::move(Keep);
        return *this;
    }

    buffer::~buffer() noexcept
    {
        Release();
    }

}