namespace xgpu::windows
{
    struct keyboard final : xgpu::details::keyboard_handle
    {
        using full_keyboard = std::array< bool, (int)xgpu::keyboard::digital::ENUM_COUNT >;

        virtual         bool       isPressedGeneric( int GadgetID ) const noexcept override
        {
            return m_KeyIsDown[GadgetID];
        }

        virtual         bool       wasPressedGeneric( int GadgetID ) const noexcept override
        {
            return m_KeyWasDown[m_KeyWasDownIndex][GadgetID];
        }

        virtual         int        getLatestChar( void ) const noexcept override
        {
            return m_MostRecentChar;
        }

        int                                     m_KeyWasDownIndex   { 0 };
        full_keyboard                           m_KeyIsDown         {};
        std::array<full_keyboard, 2>            m_KeyWasDown        {};
        WORD                                    m_MostRecentChar    { 0 };
        xgpu::keyboard::digital                 m_MostRecentKey     { xgpu::keyboard::digital::KEY_NULL };
    };
}