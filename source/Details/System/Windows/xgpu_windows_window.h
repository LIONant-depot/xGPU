
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

        virtual     bool                            isCapturing(void) const                                            noexcept override
        {
            return m_hWindow == GetCapture();
        }

        virtual     bool                            isHovered(void) const                                              noexcept override
        {
            return m_isHovered;
        }

        virtual     void                            setFocus(void) const                                               noexcept override
        {
            SetFocus(m_hWindow);
        }

        virtual     std::pair<int, int>             getPosition(void) const                                            noexcept override
        {
            //RECT Rect;
            //GetWindowRect(m_hWindow, &Rect);
            POINT Point{0,0};
            ClientToScreen(m_hWindow, &Point );
            return{ Point.x, Point.y };
        }

        virtual     void                           setPosition( int x, int y)                                          noexcept override
        {
            // Explanation:
            // hwnd: Handle to your existing window.
            // HWND_TOP : Keeps the window at the top of the Z - order.
            // x, y : New X and Y coordinates.
            // 0, 0 : Width and height(ignored because of SWP_NOSIZE).
            // SWP_NOSIZE | SWP_NOZORDER : Keeps the current size and Z - order.
            SetWindowPos(m_hWindow, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }

        virtual     void                            setSize(int Width, int Height)                                   noexcept
        {
            SetWindowPos(m_hWindow, NULL, 0, 0, Width, Height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }

        bool                            getResizedAndReset( void ) noexcept
        {
            auto b = m_isResized;
            m_isResized = false;
            return b;
        }

        bool                            isMinimized(void) const                                              noexcept
        {
            return IsIconic(m_hWindow);
        }

        void                            setMousePosition( int x, int y)                                     noexcept
        {
            SetCursorPos(x, y);
        }

        void                            setFrameless(bool frameless)                                        noexcept
        {
            m_isFrameless = frameless;
        }

        virtual                        ~window();

        HWND                                        m_hWindow   { 0 };
        std::shared_ptr<xgpu::windows::keyboard>    m_Keyboard;
        std::shared_ptr<xgpu::windows::mouse>       m_Mouse;
        int                                         m_Width     { 0 };
        int                                         m_Height    { 0 };
        bool                                        m_isMinimize{ false };
        bool                                        m_isResized { false };
        
        bool                                        m_isFrameless{ false };
        bool                                        m_isHovered{ false };
        std::pair<int, int>                         m_TruePosition{};
    };
}
