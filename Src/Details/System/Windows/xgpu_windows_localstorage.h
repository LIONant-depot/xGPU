namespace xgpu::windows
{
    struct local_storage
    {
                                                local_storage   ( void )             noexcept;
                                               ~local_storage   ( void )             noexcept;
                void                            setRaw          ( void* pPtr )       noexcept;
                void*                           getRaw          ( void )             noexcept;

        std::uint32_t   m_dwThreadIndex {};
    };
}
