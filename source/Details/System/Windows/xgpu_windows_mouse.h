namespace xgpu::windows
{
    struct mouse final : xgpu::details::mouse_handle
    {
        struct axis2
        {
            float m_X, m_Y;
        };

        using full_digital_mouse    = std::array< bool,                 (int)xgpu::mouse::digital::ENUM_COUNT >;
        using full_analog_mouse     = std::array< std::array<float, 2>, (int)xgpu::mouse::analog::ENUM_COUNT  >;


        virtual         bool       isPressedGeneric( int GadgetID ) const noexcept override
        {
            return m_ButtonIsDown[GadgetID];
        }

        virtual         bool       wasPressedGeneric( int GadgetID ) const noexcept override
        {
            return m_ButtonWasDown[m_ButtonIndex][GadgetID];
        }

        virtual         std::array<float, 2>  getValueGeneric( int GadgetID ) const noexcept override
        {
            return m_Analog[GadgetID];
        }

        int                                     m_ButtonIndex       { 0 };
        full_digital_mouse                      m_ButtonIsDown      {};
        std::array<full_digital_mouse, 2>       m_ButtonWasDown     {};
        full_analog_mouse                       m_Analog            {};
    };
}