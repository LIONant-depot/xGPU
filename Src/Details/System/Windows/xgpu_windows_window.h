
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

        virtual     bool                            isMinimized(void) const                                            noexcept override
        {
            return m_isMinimize;
        }

        HWND getWindowHandle( void ) noexcept
        {
            return m_hWindow;
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
