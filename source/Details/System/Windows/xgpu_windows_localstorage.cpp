namespace xgpu::windows
{
    //-----------------------------------------------------------------------------------------------

    local_storage::local_storage( void ) noexcept
    {
        m_dwThreadIndex = ::TlsAlloc();
        assert( m_dwThreadIndex != TLS_OUT_OF_INDEXES );
    }

    //-----------------------------------------------------------------------------------------------

    local_storage::~local_storage( void ) noexcept
    {
        ::TlsFree( m_dwThreadIndex );
    }

    //-----------------------------------------------------------------------------------------------

    void* local_storage::getRaw( void ) noexcept
    {
        return ::TlsGetValue( m_dwThreadIndex );
    }

    //-----------------------------------------------------------------------------------------------

    void local_storage::setRaw( void* pPtr ) noexcept
    {
        auto b = TlsSetValue(m_dwThreadIndex, pPtr);
        assert( b );
    }
}