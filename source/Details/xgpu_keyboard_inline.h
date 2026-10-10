#include "xgpu_virtual_input.h"

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
        if (virtual_input::Active()) return virtual_input::State().m_KeyDown[static_cast<int>(ButtonID)];                // the virtual keyboard, not the machine's
        return m_Private->isPressedGeneric( static_cast<int>(ButtonID) );
    }

    //------------------------------------------------------------------------
    [[nodiscard]] XGPU_INLINE
    bool keyboard::wasPressed( digital ButtonID ) const noexcept
    {
        if (virtual_input::Active()) return virtual_input::State().m_KeyWasDown[static_cast<int>(ButtonID)];
        return m_Private->wasPressedGeneric( static_cast<int>(ButtonID) );
    }

    //------------------------------------------------------------------------
    [[nodiscard]] XGPU_INLINE
    int keyboard::getLatestChar( void ) const noexcept
    {
        if (virtual_input::Active()) return virtual_input::State().m_LatestChar;
        return m_Private->getLatestChar();
    }

}