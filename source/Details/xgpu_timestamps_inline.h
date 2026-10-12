namespace xgpu
{
    namespace details
    {
        struct timestamps_handle
        {
            virtual        ~timestamps_handle   ( void )                                    noexcept = default;
            virtual bool    getResults          ( std::vector<std::uint64_t>& NanoSeconds ) noexcept = 0;
            virtual int     getCount            ( void ) const                              noexcept = 0;
        };
    }

    //------------------------------------------------------------------------------------------------

    XGPU_INLINE
    bool timestamps::getResults( std::vector<std::uint64_t>& NanoSeconds ) noexcept
    {
        return m_Private->getResults(NanoSeconds);
    }

    //------------------------------------------------------------------------------------------------

    XGPU_INLINE
    int timestamps::getCount( void ) const noexcept
    {
        return m_Private ? m_Private->getCount() : 0;
    }
}
