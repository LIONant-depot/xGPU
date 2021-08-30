namespace xgpu::windows
{
    //----------------------------------------------------------------------------------
    // Process Window Message Callbacks
    LRESULT CALLBACK WindowProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam ) noexcept
    {
        switch( uMsg )
        {
        case WM_CLOSE:
            PostMessage( hWnd, WM_QUIT, 0, 0 );
            return 0;

        case WM_PAINT:
            ValidateRect( hWnd, NULL );
            break;

        // This should be more precise than the regular mouse move message but in fact it seems to be less precise
        // as I can see things drift 
        /*
        case WM_INPUT:
        {
            if (auto pWin = reinterpret_cast<windows::window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA)); pWin)
            {
                UINT dwSize = sizeof(RAWINPUT);
                BYTE lpb[sizeof(RAWINPUT)];

                GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER));

                RAWINPUT* raw = (RAWINPUT*)lpb;

                if (raw->header.dwType == RIM_TYPEMOUSE)
                {
                    int xPos = raw->data.mouse.lLastX;
                    int yPos = raw->data.mouse.lLastY;
                    if (raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)
                    {
                        pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_REL)][0] = static_cast<float>(xPos) - pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_ABS)][0];
                        pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_REL)][1] = static_cast<float>(yPos) - pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_ABS)][1];

                        pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_ABS)][0] = static_cast<float>(xPos);
                        pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_ABS)][1] = static_cast<float>(yPos);
                    }
                    else
                    {
                        pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_REL)][0] = static_cast<float>(xPos);
                        pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_REL)][1] = static_cast<float>(yPos);

                        POINT pos;
                        GetCursorPos(&pos);
                        ScreenToClient(hWnd, &pos);
                        pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_ABS)][0] = static_cast<float>(pos.x);
                        pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_ABS)][1] = static_cast<float>(pos.y);
                    }
                }
            }
            break;
        }
        */
        case WM_MOUSEMOVE:
            if( auto pWin = reinterpret_cast<windows::window*>( GetWindowLongPtr( hWnd, GWLP_USERDATA ) ); pWin )
            {
                auto x = static_cast<const int>(lParam & 0xffff);
                auto y = static_cast<const int>(lParam >> 16);

                pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_REL)][0] = x - pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_ABS)][0];
                pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_REL)][1] = y - pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_ABS)][1];

                pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_ABS)][0] = static_cast<float>(x);
                pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_ABS)][1] = static_cast<float>(y);

                pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::WHEEL_REL)][0] = 0;
                pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::WHEEL_REL)][1] = 0;
            }
            break;
        case WM_MBUTTONDOWN:
            if( auto pWin = reinterpret_cast<windows::window*>( GetWindowLongPtr( hWnd, GWLP_USERDATA ) ); pWin )
            {
                SetCapture(hWnd);

                auto x = static_cast<const int>(lParam & 0xffff);
                auto y = static_cast<const int>(lParam >> 16);

                pWin->m_Mouse->m_ButtonIsDown[static_cast<int>(xgpu::mouse::digital::BTN_MIDDLE)] = 1;
                pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_ABS)][0] = static_cast<float>(x);
                pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_ABS)][1] = static_cast<float>(y);

                pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_REL)][0] = 0;
                pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_REL)][1] = 0;
            }
            break;
        case WM_MBUTTONUP:
            if( auto pWin = reinterpret_cast<windows::window*>( GetWindowLongPtr( hWnd, GWLP_USERDATA ) ); pWin )
            {
                pWin->m_Mouse->m_ButtonIsDown[static_cast<int>(xgpu::mouse::digital::BTN_MIDDLE)] = 0;
                pWin->m_Mouse->m_ButtonWasDown[ pWin->m_Mouse->m_ButtonIndex ][ static_cast<int>(xgpu::mouse::digital::BTN_MIDDLE) ] = 1;

                ReleaseCapture();
            }
            break;
        case WM_LBUTTONDOWN:
            if( auto pWin = reinterpret_cast<windows::window*>( GetWindowLongPtr( hWnd, GWLP_USERDATA ) ); pWin )
            {
                SetCapture(hWnd);

                auto x = static_cast<const int>(lParam & 0xffff);
                auto y = static_cast<const int>(lParam >> 16);

                pWin->m_Mouse->m_ButtonIsDown[static_cast<int>(xgpu::mouse::digital::BTN_LEFT)] = 1;
                pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_ABS)][0]      = static_cast<float>(x);
                pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_ABS)][1]      = static_cast<float>(y);

                pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_REL)][0] = 0;
                pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_REL)][1] = 0;
            }
            break;
        case WM_LBUTTONUP:
            if( auto pWin = reinterpret_cast<windows::window*>( GetWindowLongPtr( hWnd, GWLP_USERDATA ) ); pWin )
            {
                pWin->m_Mouse->m_ButtonIsDown[static_cast<int>(xgpu::mouse::digital::BTN_LEFT)] = 0;
                pWin->m_Mouse->m_ButtonWasDown[pWin->m_Mouse->m_ButtonIndex][static_cast<int>(xgpu::mouse::digital::BTN_LEFT)] = 1;

                ReleaseCapture();
            }
            break;
        case WM_RBUTTONDOWN:
            if( auto pWin = reinterpret_cast<windows::window*>( GetWindowLongPtr( hWnd, GWLP_USERDATA ) ); pWin )
            {
                SetCapture(hWnd);

                auto x = static_cast<const int>(lParam & 0xffff);
                auto y = static_cast<const int>(lParam >> 16);

                pWin->m_Mouse->m_ButtonIsDown[static_cast<int>(xgpu::mouse::digital::BTN_RIGHT)] = 1;
                pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_ABS)][0] = static_cast<float>(x);
                pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_ABS)][1] = static_cast<float>(y);

                pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_REL)][0] = 0;
                pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::POS_REL)][1] = 0;
            }
            break;
        case WM_RBUTTONUP:
            if( auto pWin = reinterpret_cast<windows::window*>( GetWindowLongPtr( hWnd, GWLP_USERDATA ) ); pWin )
            {
                pWin->m_Mouse->m_ButtonIsDown[static_cast<int>(xgpu::mouse::digital::BTN_RIGHT)] = 0;
                pWin->m_Mouse->m_ButtonWasDown[pWin->m_Mouse->m_ButtonIndex][static_cast<int>(xgpu::mouse::digital::BTN_RIGHT)] = 1;

                ReleaseCapture();
            }
            break;
        case WM_KEYDOWN:
            if( auto pWin = reinterpret_cast<windows::window*>( GetWindowLongPtr( hWnd, GWLP_USERDATA ) ); pWin )
            {
                const auto Code = OSKeyToKeyboardInputKeyCode( static_cast<int>(wParam) );
                if ( Code >= static_cast<int>(xgpu::keyboard::digital::KEY_NULL) && Code < static_cast<int>(xgpu::keyboard::digital::ENUM_COUNT) )
                {
                    pWin->m_Keyboard->m_KeyIsDown[Code] = true;
                    pWin->m_Keyboard->m_MostRecentKey   = static_cast<xgpu::keyboard::digital>(Code);

                    pWin->m_Keyboard->m_KeyIsDown[static_cast<int>(xgpu::keyboard::digital::KEY_LCONTROL) ] = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    pWin->m_Keyboard->m_KeyIsDown[static_cast<int>(xgpu::keyboard::digital::KEY_LSHIFT)   ] = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
                    pWin->m_Keyboard->m_KeyIsDown[static_cast<int>(xgpu::keyboard::digital::KEY_LALT)     ] = (GetKeyState(VK_MENU)    & 0x8000) != 0;

                    // TODO: Optimize this....
                    // Get the ascii character
                    BYTE kbs[256]={0};
                    GetKeyboardState(kbs);
                    if(kbs[VK_CONTROL] & 0x00000080)
                    {
                        kbs[VK_CONTROL] &= 0x0000007f;
                        ::ToAscii( static_cast<std::uint32_t>(wParam), ::MapVirtualKey( static_cast<std::uint32_t>(wParam), MAPVK_VK_TO_VSC), kbs, &pWin->m_Keyboard->m_MostRecentChar, 0);
                        kbs[VK_CONTROL] |= 0x00000080;
                    }
                    else
                    {
                        ::ToAscii( static_cast<std::uint32_t>(wParam), ::MapVirtualKey( static_cast<std::uint32_t>(wParam), MAPVK_VK_TO_VSC), kbs, &pWin->m_Keyboard->m_MostRecentChar, 0);
                    }
                }
            }
            break;
        case WM_KEYUP:
            if( auto pWin = reinterpret_cast<windows::window*>( GetWindowLongPtr( hWnd, GWLP_USERDATA ) ); pWin )
            {
                const auto Code = OSKeyToKeyboardInputKeyCode( static_cast<int>(wParam) );
                if ( Code >= static_cast<int>(xgpu::keyboard::digital::KEY_NULL) && Code < static_cast<int>(xgpu::keyboard::digital::ENUM_COUNT) )
                {
                    pWin->m_Keyboard->m_KeyIsDown[Code] = false;
                    if( pWin->m_Keyboard->m_MostRecentKey == static_cast<xgpu::keyboard::digital>(Code) ) 
                    {
                        pWin->m_Keyboard->m_MostRecentChar = 0;
                        pWin->m_Keyboard->m_MostRecentKey  = xgpu::keyboard::digital::KEY_NULL;
                    }

                    pWin->m_Keyboard->m_KeyIsDown[static_cast<int>(xgpu::keyboard::digital::KEY_LCONTROL) ] = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    pWin->m_Keyboard->m_KeyIsDown[static_cast<int>(xgpu::keyboard::digital::KEY_LSHIFT)   ] = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
                    pWin->m_Keyboard->m_KeyIsDown[static_cast<int>(xgpu::keyboard::digital::KEY_LALT)     ] = (GetKeyState(VK_MENU)    & 0x8000) != 0;

                    pWin->m_Keyboard->m_KeyWasDown[pWin->m_Keyboard->m_KeyWasDownIndex][Code] = 1;
                }
            }
            break;
        case WM_SIZE:
            if( auto pWin = reinterpret_cast<windows::window*>( GetWindowLongPtr( hWnd, GWLP_USERDATA ) ); pWin )
            {
                // Resize the application to the new window size, except when
                // it was minimized. Vulkan doesn't support images or swapchains
                // with width=0 and height=0.
                if (wParam != SIZE_MINIMIZED) 
                {
                    pWin->m_Height     = static_cast<int>((lParam & 0xffff0000) >> 16);
                    pWin->m_Width      = static_cast<int>(lParam & 0xffff);
                    pWin->m_isResized  = true;
                    pWin->m_isMinimize = false;
                    return 0;
                }
                else
                {
                    pWin->m_isMinimize = true;
                }
            }
            break;
        case WM_MOUSEWHEEL:
            if( auto pWin = reinterpret_cast<windows::window*>( GetWindowLongPtr( hWnd, GWLP_USERDATA ) ); pWin )
            {
                auto   fwKeys = GET_KEYSTATE_WPARAM(wParam);
                auto   zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
                float& V      = pWin->m_Mouse->m_Analog[static_cast<int>(xgpu::mouse::analog::WHEEL_REL)][0];
                V             = (zDelta*100.0f)/std::numeric_limits<short>::max();
            }
            break;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
                return 0;
            break;


        }    // End switch

        // Pass Unhandled Messages To DefWindowProc
        return DefWindowProc (hWnd, uMsg, wParam, lParam);
    }

    //----------------------------------------------------------------------------------

    static
    xgpu::device::error* CreateWindowClass( HINSTANCE hinstance, WNDPROC wndproc ) noexcept
    {
        //
        // Check if we already register the class
        //
        {
            WNDCLASSA C{};
            if (GetClassInfoA(hinstance, "LIONClass", &C))
            {
                return nullptr;
            }
        }

        WNDCLASSEX wndClass;
        wndClass.cbSize        = sizeof(WNDCLASSEX);
        wndClass.style         = CS_HREDRAW | CS_VREDRAW;
        wndClass.lpfnWndProc   = wndproc;
        wndClass.cbClsExtra    = 0;
        wndClass.cbWndExtra    = 0;
        wndClass.hInstance     = hinstance;
        wndClass.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
        wndClass.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wndClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        wndClass.lpszMenuName  = NULL;
        wndClass.lpszClassName = TEXT("LIONClass");
        wndClass.hIconSm       = LoadIcon(NULL, IDI_WINLOGO);

        if (!RegisterClassEx(&wndClass))
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Could not register window class!" );

        return nullptr;
    }

    //--------------------------------------------------------------------------------------------

    static
    xgpu::device::error* CreateSytemWindow(
        HINSTANCE   hInstance,
        HWND&       hWnd,
        bool        bFullScreen,
        int         width,
        int         height)
    {
        //
        // Get Resolution
        //
        const int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
        const int screenHeight = GetSystemMetrics(SM_CYSCREEN);

        if (bFullScreen)
        {
            DEVMODE dmScreenSettings{};
            memset(&dmScreenSettings, 0, sizeof(dmScreenSettings));
            dmScreenSettings.dmSize         = sizeof(dmScreenSettings);
            dmScreenSettings.dmPelsWidth    = screenWidth;
            dmScreenSettings.dmPelsHeight   = screenHeight;
            dmScreenSettings.dmBitsPerPel   = 32;
            dmScreenSettings.dmFields       = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;

            if ((width != screenWidth) && (height != screenHeight))
            {
                if (ChangeDisplaySettings(&dmScreenSettings, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)
                    return VGPU_ERROR( xgpu::device::error::FAILURE, "Fullscreen Mode not supported!");
            }
        }

        //
        // Compute windows flags
        //
        const DWORD dwExStyle = bFullScreen ? WS_EX_APPWINDOW                               : WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
        const DWORD dwStyle   = bFullScreen ? WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN  : WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

        //
        // Determine window rectangle
        //
        RECT windowRect;
        if (bFullScreen)
        {
            windowRect.left   = static_cast<long>(0);
            windowRect.right  = static_cast<long>(screenWidth);
            windowRect.top    = static_cast<long>(0);
            windowRect.bottom = static_cast<long>(screenHeight);
        }
        else
        {
            windowRect.left   = static_cast<long>(screenWidth) / 2 - width/2;
            windowRect.right  = static_cast<long>(width);
            windowRect.top    = static_cast<long>(screenHeight) / 2 - height/2;
            windowRect.bottom = static_cast<long>(height);
        }

        AdjustWindowRectEx( &windowRect, dwStyle, FALSE, dwExStyle );

        //
        // Create Window
        //
        hWnd = CreateWindowEx
        (
            0
        ,   TEXT("LIONClass")
        ,   TEXT("LION")
        ,   dwStyle | WS_CLIPSIBLINGS | WS_CLIPCHILDREN
        ,   windowRect.left
        ,   windowRect.top
        ,   windowRect.right
        ,   windowRect.bottom
        ,   NULL
        ,   NULL
        ,   hInstance
        ,   NULL
        );

        if (!hWnd)
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create a window!" );

        ShowWindow( hWnd, SW_SHOW );
        SetForegroundWindow( hWnd );
        SetFocus( hWnd );

        return nullptr;
    }

    //----------------------------------------------------------------------------------

    xgpu::device::error* window::Initialize( const xgpu::window::setup& Setup, xgpu::windows::instance& Instance ) noexcept
    {
        const auto hInstance = GetModuleHandle(NULL);

        if (auto Err = CreateWindowClass(hInstance, WindowProc); Err )
            return Err;
      
        if ( auto Err = CreateSytemWindow(hInstance, m_hWindow, Setup.m_bFullScreen, Setup.m_Width, Setup.m_Height); Err )
            return Err;

        if (m_hWindow) SetWindowLongPtr(m_hWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        m_Width    = Setup.m_Width;
        m_Height   = Setup.m_Height;
        m_Mouse    = Instance.m_Mouse;
        m_Keyboard = Instance.m_Keyboard;

        //
        // Allow us to track the mouse outside the window (get WM_INPUT messages) for the mouse
        //
        if constexpr (false)
        {
            #ifndef HID_USAGE_PAGE_GENERIC
                #define HID_USAGE_PAGE_GENERIC         ((USHORT) 0x01)
            #endif
            #ifndef HID_USAGE_GENERIC_MOUSE
                #define HID_USAGE_GENERIC_MOUSE        ((USHORT) 0x02)
            #endif

            RAWINPUTDEVICE Rid[1];
            Rid[0].usUsagePage  = HID_USAGE_PAGE_GENERIC;
            Rid[0].usUsage      = HID_USAGE_GENERIC_MOUSE;
            Rid[0].dwFlags      = RIDEV_INPUTSINK;
            Rid[0].hwndTarget   = m_hWindow;
            RegisterRawInputDevices(Rid, 1, sizeof(Rid[0]));
        }

        return nullptr;
    }
}