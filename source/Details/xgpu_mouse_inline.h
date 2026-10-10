#include "xgpu_virtual_input.h"

namespace xgpu
{
    namespace details
    {
        struct mouse_handle
        {
            virtual                        ~mouse_handle        ( void         )                = default;
            virtual bool                    isPressedGeneric    ( int GadgetID ) const noexcept = 0;
            virtual bool                    wasPressedGeneric   ( int GadgetID ) const noexcept = 0;
            virtual std::array<float, 2>    getValueGeneric     ( int GadgetID ) const noexcept = 0;
        };
    }

    //----------------------------------------------------------------------------
    [[nodiscard]] XGPU_INLINE
    bool mouse::isPressed( digital ButtonID ) const noexcept
    {
        if (virtual_input::Active()) return virtual_input::State().m_ButtonDown[static_cast<int>(ButtonID)];            // the virtual mouse, not the machine's
        return m_Private->isPressedGeneric(static_cast<int>(ButtonID));
    }

    //----------------------------------------------------------------------------
    [[nodiscard]] XGPU_INLINE
    bool mouse::wasPressed( digital ButtonID ) const noexcept
    {
        if (virtual_input::Active()) return virtual_input::State().m_ButtonWasDown[static_cast<int>(ButtonID)];
        return m_Private->wasPressedGeneric(static_cast<int>(ButtonID));
    }

    //----------------------------------------------------------------------------
    [[nodiscard]] XGPU_INLINE
    std::array<float, 2> mouse::getValue( analog  PosID) const noexcept
    {
        if (virtual_input::Active())
        {
            const auto& S = virtual_input::State();
            switch (PosID)
            {
            case analog::POS_ABS:   return S.m_Position;
            case analog::POS_REL:   return S.m_Relative;
            case analog::WHEEL_REL: return { S.m_Wheel, 0.0f };
            default:                return { 0.0f, 0.0f };
            }
        }
        return m_Private->getValueGeneric(static_cast<int>(PosID));
    }

}