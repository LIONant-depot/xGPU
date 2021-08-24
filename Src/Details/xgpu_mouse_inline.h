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
    XGPU_INLINE
    [[nodiscard]] bool mouse::isPressed( digital ButtonID ) const noexcept
    {
        return m_Private->isPressedGeneric(static_cast<int>(ButtonID));
    }

    //----------------------------------------------------------------------------
    XGPU_INLINE
    [[nodiscard]] bool mouse::wasPressed( digital ButtonID ) const noexcept
    {
        return m_Private->wasPressedGeneric(static_cast<int>(ButtonID));
    }

    //----------------------------------------------------------------------------
    XGPU_INLINE
    [[nodiscard]] std::array<float, 2> mouse::getValue( analog  PosID) const noexcept
    {
        return m_Private->getValueGeneric(static_cast<int>(PosID));
    }

}