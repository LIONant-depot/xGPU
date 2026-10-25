namespace xgpu
{
    namespace details
    {
        struct texture_table_handle
        {
            virtual                                    ~texture_table_handle    ( void )                                        noexcept = default;
            virtual texture_table::error*                      Update                  ( int iSlot, const texture_table::slot& Slot )  noexcept = 0;
            virtual int                                 getSlotCount            ( void ) const                                  noexcept = 0;
            virtual texture_table::slot                 getSlot                 ( int iSlot ) const                             noexcept = 0;
            virtual void                                DeathMarch              ( xgpu::texture_table&& )                       noexcept = 0;
        };
    }

    //-----------------------------------------------------------------------------

    texture_table::error* texture_table::Update( int iSlot, const slot& Slot ) noexcept
    {
        return m_Private->Update(iSlot, Slot);
    }

    //-----------------------------------------------------------------------------

    int texture_table::getSlotCount( void ) const noexcept
    {
        return m_Private->getSlotCount();
    }

    //-----------------------------------------------------------------------------

    texture_table::slot texture_table::getSlot( int iSlot ) const noexcept
    {
        return m_Private->getSlot(iSlot);
    }

    //-----------------------------------------------------------------------------

    texture_table::~texture_table( void ) noexcept
    {
        if (m_Private.use_count() > 1) return;
        if (!m_Private) return;
        m_Private->DeathMarch(std::move(*this));
        m_Private.reset();
    }
}
