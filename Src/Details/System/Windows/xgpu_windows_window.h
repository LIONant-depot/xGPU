
namespace xgpu::windows
{
    struct window : xgpu::details::window_handle
    {
        xgpu::device::error* Initialize( const xgpu::window::setup& Setup, xgpu::windows::instance& Instance ) noexcept;

        virtual     int                             getWidth(void) const                                              noexcept override
        {
            return m_Width;
        }

        virtual     int                             getHeight(void) const                                              noexcept override
        {
            return m_Height;
        }

        virtual     bool                            BegingRendering(void)                                              noexcept override
        {
            return m_isMinimize;
        }

        virtual     std::size_t                     getSystemWindowHandle(void) const                                  noexcept override
        {
            return reinterpret_cast<std::size_t>(m_hWindow);
        }

        virtual     bool                            isFocused(void) const                                              noexcept override
        {
            return m_hWindow == GetFocus();
        }

        virtual     std::pair<int, int>             getPosition(void) const                                            noexcept override
        {
            //RECT Rect;
            //GetWindowRect(m_hWindow, &Rect);
            POINT Point{0,0};
            ClientToScreen(m_hWindow, &Point );
            return{ Point.x, Point.y };
        }

        bool                            getResizedAndReset( void ) noexcept
        {
            auto b = m_isResized;
            m_isResized = false;
            return b;
        }

        HWND                                        m_hWindow   { 0 };
        std::shared_ptr<xgpu::windows::keyboard>    m_Keyboard;
        std::shared_ptr<xgpu::windows::mouse>       m_Mouse;
        int                                         m_Width     { 0 };
        int                                         m_Height    { 0 };
        bool                                        m_isMinimize{ false };
        bool                                        m_isResized { false };
    };
}
