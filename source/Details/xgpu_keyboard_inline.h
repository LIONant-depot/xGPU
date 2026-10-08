namespace xgpu
{
    namespace details
    {
        struct keyboard_handle
        {
            virtual                ~keyboard_handle     ( void )                        = default;
            virtual bool            isPressedGeneric    ( int GadgetID ) const noexcept = 0;
            virtual bool            wasPressedGeneric   ( int GadgetID ) const noexcept = 0;
            virtual int             getLatestChar       ( void )         const noexcept = 0;
        };
    }

    //------------------------------------------------------------------------
    [[nodiscard]] XGPU_INLINE
    bool keyboard::isPressed( digital ButtonID ) const noexcept
    {
        return m_Private->isPressedGeneric( static_cast<int>(ButtonID) );
    }

    //------------------------------------------------------------------------
    [[nodiscard]] XGPU_INLINE
    bool keyboard::wasPressed( digital ButtonID ) const noexcept
    {
        return m_Private->wasPressedGeneric( static_cast<int>(ButtonID) );
    }

    //------------------------------------------------------------------------
    [[nodiscard]] XGPU_INLINE
    int keyboard::getLatestChar( void ) const noexcept
    {
        return m_Private->getLatestChar();
    }

}