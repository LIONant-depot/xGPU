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
    XGPU_INLINE
    [[nodiscard]] bool keyboard::isPressed( digital ButtonID ) const noexcept
    {
        return m_Private->isPressedGeneric( static_cast<int>(ButtonID) );
    }

    //------------------------------------------------------------------------
    XGPU_INLINE
    [[nodiscard]] bool keyboard::wasPressed( digital ButtonID ) const noexcept
    {
        return m_Private->wasPressedGeneric( static_cast<int>(ButtonID) );
    }

    //------------------------------------------------------------------------
    XGPU_INLINE
    [[nodiscard]] int keyboard::getLatestChar( void ) const noexcept
    {
        return m_Private->getLatestChar();
    }

}