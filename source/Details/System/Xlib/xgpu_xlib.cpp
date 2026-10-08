#include "xgpu_xlib.h"

#include <unordered_map>
#include <mutex>
#include <vector>
#include <string_view>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <vulkan/vulkan_xlib.h>     // VkXlibSurfaceCreateInfoKHR: the real header (vulkan.h only includes it behind VK_USE_PLATFORM_XLIB_KHR)

// Xlib #defines these as plain macros; they would break the Vulkan layer that is compiled after us in the same
// translation unit (and anything else that has an enumerator or a type called like them).
#undef None
#undef Bool
#undef Status
#undef True
#undef False
#undef Always
#undef Success
#undef KeyPress
#undef KeyRelease
#undef ButtonPress
#undef ButtonRelease
#undef MotionNotify
#undef EnterNotify
#undef LeaveNotify
#undef FocusIn
#undef FocusOut
#undef MapNotify
#undef UnmapNotify
#undef ConfigureNotify
#undef ClientMessage
#undef CurrentTime
#undef DestroyAll
#undef Complex
#undef Convex
#undef Above
#undef Below
#undef Opposite
#undef RevertToParent
#undef CopyFromParent
#undef InputOutput

// Keep the values we need after the #undefs above (the Xlib macros expand to these numbers, see X.h)
namespace xgpu::xlib::x
{
    inline constexpr int              KeyPress           = 2;
    inline constexpr int              KeyRelease         = 3;
    inline constexpr int              ButtonPress        = 4;
    inline constexpr int              ButtonRelease      = 5;
    inline constexpr int              MotionNotify       = 6;
    inline constexpr int              EnterNotify        = 7;
    inline constexpr int              LeaveNotify        = 8;
    inline constexpr int              FocusIn            = 9;
    inline constexpr int              FocusOut           = 10;
    inline constexpr int              UnmapNotify        = 18;
    inline constexpr int              MapNotify          = 19;
    inline constexpr int              ConfigureNotify    = 22;
    inline constexpr int              ClientMessage      = 33;
    inline constexpr int              ErrBadMatch       = 8;
    inline constexpr int              SetInputFocusReq   = 42;
    inline constexpr unsigned long    CurrentTime        = 0;
    inline constexpr int              RevertToParent     = 2;
    inline constexpr int              InputOutput        = 1;
    inline constexpr long             NoMask             = 0;
}

namespace xgpu::xlib
{
    //----------------------------------------------------------------------------------
    // The X connection. One per instance, shared with every window so a window can safely outlive the instance
    // object while it is being torn down.
    //----------------------------------------------------------------------------------
    struct connection
    {
        Display*                                    m_pDisplay          { nullptr };
        int                                         m_Screen            { 0 };
        ::Window                                    m_Root              { 0 };
        Atom                                        m_WMProtocols       { 0 };
        Atom                                        m_WMDeleteWindow    { 0 };
        Atom                                        m_NetWMState        { 0 };
        Atom                                        m_NetWMFullscreen   { 0 };
        Atom                                        m_MotifWMHints      { 0 };
        std::unordered_map<unsigned long, window*>  m_Windows           {};

        ~connection( void ) noexcept { if( m_pDisplay ) XCloseDisplay(m_pDisplay); }
    };

    static XErrorHandler s_PrevErrorHandler = nullptr;

    // Xlib's default handler exits the process. A focus request on a window that is not viewable yet is a harmless
    // BadMatch (the user can click away at any time), so swallow just that and leave everything else to the previous handler.
    static
    int ErrorHandler( Display* pDisplay, XErrorEvent* pError ) noexcept
    {
        if( pError->request_code == x::SetInputFocusReq && pError->error_code == x::ErrBadMatch ) return 0;
        return s_PrevErrorHandler ? s_PrevErrorHandler(pDisplay, pError) : 0;
    }

    //----------------------------------------------------------------------------------

    std::shared_ptr<connection> instance::getConnection( void ) noexcept
    {
        if( m_bConnectionTried ) return m_Connection;
        m_bConnectionTried = true;

        static std::once_flag s_Once;
        std::call_once( s_Once, []{ XInitThreads(); s_PrevErrorHandler = XSetErrorHandler(ErrorHandler); } );

        Display* pDisplay = XOpenDisplay(nullptr);
        if( pDisplay == nullptr ) return nullptr;

        auto Connection             = std::make_shared<connection>();
        Connection->m_pDisplay      = pDisplay;
        Connection->m_Screen        = DefaultScreen(pDisplay);
        Connection->m_Root          = RootWindow(pDisplay, Connection->m_Screen);
        Connection->m_WMProtocols   = XInternAtom(pDisplay, "WM_PROTOCOLS",             0);
        Connection->m_WMDeleteWindow= XInternAtom(pDisplay, "WM_DELETE_WINDOW",         0);
        Connection->m_NetWMState    = XInternAtom(pDisplay, "_NET_WM_STATE",            0);
        Connection->m_NetWMFullscreen = XInternAtom(pDisplay, "_NET_WM_STATE_FULLSCREEN", 0);
        Connection->m_MotifWMHints  = XInternAtom(pDisplay, "_MOTIF_WM_HINTS",          0);

        // Holding a key must not look like release+press pairs (Windows only sends repeated presses)
        int Supported = 0;
        XkbSetDetectableAutoRepeat(pDisplay, 1, &Supported);

        m_Connection = std::move(Connection);
        return m_Connection;
    }

    //----------------------------------------------------------------------------------
    // Window
    //----------------------------------------------------------------------------------

    std::span<const char* const> instance::getSurfaceExtensions( void ) noexcept
    {
        static const std::array<const char*, 1> s_List { VK_KHR_XLIB_SURFACE_EXTENSION_NAME };
        static const bool bSupported = []
        {
            std::uint32_t Count = 0;
            if( vkEnumerateInstanceExtensionProperties(nullptr, &Count, nullptr) != VK_SUCCESS ) return false;
            std::vector<VkExtensionProperties> Available(Count);
            if( vkEnumerateInstanceExtensionProperties(nullptr, &Count, Available.data()) < 0 ) return false;
            for( auto& E : Available ) if( std::string_view(E.extensionName) == VK_KHR_XLIB_SURFACE_EXTENSION_NAME ) return true;
            return false;
        }();
        return bSupported ? std::span<const char* const>(s_List) : std::span<const char* const>();
    }

    //----------------------------------------------------------------------------------

    VkResult window::CreateVulkanSurface( VkInstance Instance, VkSurfaceKHR& Surface ) const noexcept
    {
        auto pfnCreate = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(vkGetInstanceProcAddr(Instance, "vkCreateXlibSurfaceKHR"));
        if( pfnCreate == nullptr ) return VK_ERROR_EXTENSION_NOT_PRESENT;

        const VkXlibSurfaceCreateInfoKHR Info
        {
            .sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR
        ,   .pNext  = nullptr
        ,   .flags  = 0
        ,   .dpy    = m_Connection->m_pDisplay
        ,   .window = m_Window
        };
        return pfnCreate(Instance, &Info, nullptr, &Surface);
    }

    //----------------------------------------------------------------------------------

    xgpu::device::error* window::Initialize( const xgpu::window::setup& Setup, instance& Instance ) noexcept
    {
        m_Connection = Instance.getConnection();
        if( m_Connection == nullptr )
            return VGPU_ERROR(xgpu::device::error::FAILURE, "xGPU: could not open the X display (is DISPLAY set and an X server running?)");

        Display* pDisplay = m_Connection->m_pDisplay;

        const int ScreenW = DisplayWidth (pDisplay, m_Connection->m_Screen);
        const int ScreenH = DisplayHeight(pDisplay, m_Connection->m_Screen);

        const int W = Setup.m_bFullScreen ? ScreenW : Setup.m_Width;
        const int H = Setup.m_bFullScreen ? ScreenH : Setup.m_Height;
        const int X = Setup.m_bFullScreen ? 0       : (Setup.m_X < 0 ? (ScreenW - W)/2 : Setup.m_X);
        const int Y = Setup.m_bFullScreen ? 0       : (Setup.m_Y < 0 ? (ScreenH - H)/2 : Setup.m_Y);

        XSetWindowAttributes Attributes{};
        Attributes.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask
                              | EnterWindowMask | LeaveWindowMask | StructureNotifyMask | FocusChangeMask;

        m_Window = XCreateWindow( pDisplay, m_Connection->m_Root, X, Y, static_cast<unsigned>(W), static_cast<unsigned>(H), 0
                                , DefaultDepth(pDisplay, m_Connection->m_Screen), x::InputOutput
                                , DefaultVisual(pDisplay, m_Connection->m_Screen), CWEventMask, &Attributes );
        if( m_Window == 0 )
            return VGPU_ERROR(xgpu::device::error::FAILURE, "xGPU: fail to create a window!");

        // Title and class
        XStoreName(pDisplay, m_Window, "LION");
        {
            char Name[] = "lion", Class[] = "LIONClass";
            XClassHint Hint{ Name, Class };
            XSetClassHint(pDisplay, m_Window, &Hint);
        }

        // Ask the window manager to tell us about the close button instead of killing the connection
        Atom Protocols[] = { m_Connection->m_WMDeleteWindow };
        XSetWMProtocols(pDisplay, m_Window, Protocols, 1);

        // Honor the requested position and size
        {
            XSizeHints Hints{};
            Hints.flags  = USPosition | USSize | PPosition | PSize;
            Hints.x      = X;      Hints.y      = Y;
            Hints.width  = W;      Hints.height = H;
            XSetWMNormalHints(pDisplay, m_Window, &Hints);
        }

        // Do not take the focus when the user asked for a window that does not want it
        if( !Setup.m_bFocus )
        {
            XWMHints Hints{};
            Hints.flags = InputHint;
            Hints.input = 0;
            XSetWMHints(pDisplay, m_Window, &Hints);
        }

        // Frameless: ask for no decorations (Motif hint, understood by every common window manager)
        if( Setup.m_bFrameless && !Setup.m_bFullScreen )
        {
            struct { unsigned long Flags, Functions, Decorations; long InputMode; unsigned long Status; } Motif
            { 2 /*MWM_HINTS_DECORATIONS*/, 0, 0, 0, 0 };
            XChangeProperty( pDisplay, m_Window, m_Connection->m_MotifWMHints, m_Connection->m_MotifWMHints, 32, PropModeReplace
                           , reinterpret_cast<unsigned char*>(&Motif), 5 );
        }

        // Full screen (EWMH): the property is honored when set before the window is mapped
        if( Setup.m_bFullScreen )
        {
            Atom State = m_Connection->m_NetWMFullscreen;
            XChangeProperty( pDisplay, m_Window, m_Connection->m_NetWMState, XA_ATOM, 32, PropModeReplace
                           , reinterpret_cast<unsigned char*>(&State), 1 );
        }

        m_Width        = W;
        m_Height       = H;
        m_Mouse        = Instance.m_Mouse;
        m_Keyboard     = Instance.m_Keyboard;
        m_isFrameless  = Setup.m_bFrameless;
        m_TruePosition = { X, Y };

        m_Connection->m_Windows[m_Window] = this;

        XMapWindow(pDisplay, m_Window);
        XFlush(pDisplay);
        return nullptr;
    }

    //----------------------------------------------------------------------------------

    window::~window( void ) noexcept
    {
        if( m_Connection && m_Window )
        {
            m_Connection->m_Windows.erase(m_Window);
            XDestroyWindow(m_Connection->m_pDisplay, m_Window);
            XFlush(m_Connection->m_pDisplay);
        }
    }

    //----------------------------------------------------------------------------------

    void window::setFocus( void ) const noexcept
    {
        XSetInputFocus(m_Connection->m_pDisplay, m_Window, x::RevertToParent, x::CurrentTime);
        XFlush(m_Connection->m_pDisplay);
    }

    //----------------------------------------------------------------------------------

    std::pair<int, int> window::getPosition( void ) const noexcept
    {
        int         X = 0, Y = 0;
        ::Window    Child;
        XTranslateCoordinates( m_Connection->m_pDisplay, m_Window, m_Connection->m_Root, 0, 0, &X, &Y, &Child );
        return { X, Y };
    }

    //----------------------------------------------------------------------------------

    void window::setPosition( int X, int Y ) noexcept
    {
        XMoveWindow(m_Connection->m_pDisplay, m_Window, X, Y);
        XFlush(m_Connection->m_pDisplay);
    }

    //----------------------------------------------------------------------------------

    void window::setSize( int Width, int Height ) noexcept
    {
        XResizeWindow(m_Connection->m_pDisplay, m_Window, static_cast<unsigned>(Width), static_cast<unsigned>(Height));
        XFlush(m_Connection->m_pDisplay);
    }

    //----------------------------------------------------------------------------------

    void window::setMousePosition( int X, int Y ) noexcept   // screen coordinates, as SetCursorPos
    {
        XWarpPointer(m_Connection->m_pDisplay, 0, m_Connection->m_Root, 0, 0, 0, 0, X, Y);
        XFlush(m_Connection->m_pDisplay);
    }

    //----------------------------------------------------------------------------------
    // Events
    //----------------------------------------------------------------------------------

    // One wheel notch is 120 on Windows, scaled to the same range the Windows backend reports
    static constexpr float s_WheelNotch = (120.0f * 100.0f) / 32767.0f;

    static
    void SetModifiers( linux_os::keyboard& K ) noexcept
    {
        using k = xgpu::keyboard::digital;
        constexpr std::array Pairs { std::pair{k::KEY_LCONTROL, k::KEY_RCONTROL}, std::pair{k::KEY_LSHIFT, k::KEY_RSHIFT}, std::pair{k::KEY_LALT, k::KEY_RALT} };
        for( auto [L, R] : Pairs )
        {
            K.m_KeyIsDown[(int)L] = K.m_KeyPhysical[(int)L] || K.m_KeyPhysical[(int)R];
            K.m_KeyIsDown[(int)R] = K.m_KeyPhysical[(int)R];
        }
    }

    static
    bool Dispatch( connection& C, XEvent& E ) noexcept
    {
        auto It = C.m_Windows.find( E.xany.window );
        if( It == C.m_Windows.end() ) return true;
        window& W = *It->second;
        auto&   M = *W.m_Mouse;
        auto&   K = *W.m_Keyboard;

        constexpr auto POS_REL   = static_cast<int>(xgpu::mouse::analog::POS_REL);
        constexpr auto POS_ABS   = static_cast<int>(xgpu::mouse::analog::POS_ABS);
        constexpr auto WHEEL_REL = static_cast<int>(xgpu::mouse::analog::WHEEL_REL);

        switch( E.type )
        {
        case x::ClientMessage:
            if( E.xclient.message_type == C.m_WMProtocols && static_cast<Atom>(E.xclient.data.l[0]) == C.m_WMDeleteWindow )
                return false;           // the close button: ends the loop, same as WM_CLOSE -> WM_QUIT
            break;

        case x::MotionNotify:
            {
                const float X = static_cast<float>(E.xmotion.x);
                const float Y = static_cast<float>(E.xmotion.y);
                M.m_Analog[POS_REL][0] += X - M.m_Analog[POS_ABS][0];
                M.m_Analog[POS_REL][1] += Y - M.m_Analog[POS_ABS][1];
                M.m_Analog[POS_ABS][0]  = X;
                M.m_Analog[POS_ABS][1]  = Y;
                W.m_isHovered           = true;
            }
            break;

        case x::EnterNotify: W.m_isHovered = true;  break;
        case x::LeaveNotify: W.m_isHovered = false; break;

        case x::ButtonPress:
        case x::ButtonRelease:
            {
                const bool bDown = E.type == x::ButtonPress;
                const int  Btn   = static_cast<int>(E.xbutton.button);

                // Wheel: buttons 4/5 vertical, 6/7 horizontal; they only come as a press
                if( Btn >= 4 && Btn <= 7 )
                {
                    if( bDown )
                    {
                        const float Dir = (Btn == 4 || Btn == 7) ? s_WheelNotch : -s_WheelNotch;
                        M.m_Analog[WHEEL_REL][ Btn <= 5 ? 0 : 1 ] += Dir;
                    }
                    break;
                }

                int Digital = -1;
                switch( Btn )
                {
                case 1: Digital = (int)xgpu::mouse::digital::BTN_LEFT;   break;
                case 2: Digital = (int)xgpu::mouse::digital::BTN_MIDDLE; break;
                case 3: Digital = (int)xgpu::mouse::digital::BTN_RIGHT;  break;
                case 8: Digital = (int)xgpu::mouse::digital::BTN_0;      break;
                case 9: Digital = (int)xgpu::mouse::digital::BTN_1;      break;
                }
                if( Digital < 0 ) break;

                if( bDown )
                {
                    // X grabs the pointer for us until the button is released (Windows' SetCapture)
                    ++W.m_ButtonsDown;
                    W.setFocus();

                    M.m_ButtonIsDown[Digital] = true;
                    M.m_Analog[POS_ABS][0]    = static_cast<float>(E.xbutton.x);
                    M.m_Analog[POS_ABS][1]    = static_cast<float>(E.xbutton.y);
                    M.m_Analog[POS_REL][0]    = 0;
                    M.m_Analog[POS_REL][1]    = 0;
                }
                else
                {
                    if( W.m_ButtonsDown > 0 ) --W.m_ButtonsDown;
                    M.m_ButtonIsDown[Digital]                         = false;
                    M.m_ButtonWasDown[M.m_ButtonIndex][Digital]       = true;
                }
            }
            break;

        case x::KeyPress:
        case x::KeyRelease:
            {
                const auto Key = linux_os::EvdevToKey( static_cast<int>(E.xkey.keycode) - 8 );
                if( Key == xgpu::keyboard::digital::KEY_NULL ) break;
                const int Code = static_cast<int>(Key);

                if( E.type == x::KeyPress )
                {
                    K.m_KeyPhysical[Code] = true;
                    K.m_KeyIsDown  [Code] = true;
                    K.m_MostRecentKey     = Key;
                    SetModifiers(K);

                    // The ascii character. With Ctrl held ask for the key itself (not the control code), as the Windows backend does
                    XKeyEvent Copy  = E.xkey;
                    Copy.state     &= ~static_cast<unsigned>(ControlMask);
                    char   Buffer[8]= {};
                    KeySym Sym      = 0;
                    const int n     = XLookupString(&Copy, Buffer, sizeof(Buffer), &Sym, nullptr);
                    K.m_MostRecentChar = (n == 1 && static_cast<unsigned char>(Buffer[0]) < 128) ? Buffer[0] : 0;
                }
                else
                {
                    K.m_KeyPhysical[Code] = false;
                    K.m_KeyIsDown  [Code] = false;
                    if( K.m_MostRecentKey == Key )
                    {
                        K.m_MostRecentChar = 0;
                        K.m_MostRecentKey  = xgpu::keyboard::digital::KEY_NULL;
                    }
                    SetModifiers(K);

                    K.m_KeyWasDown[K.m_KeyWasDownIndex][Code] = true;

                    // Windows has a single Ctrl/Shift/Alt; releasing either side counts as releasing "the" modifier
                    using k = xgpu::keyboard::digital;
                    if( Key == k::KEY_RCONTROL ) K.m_KeyWasDown[K.m_KeyWasDownIndex][(int)k::KEY_LCONTROL] = true;
                    if( Key == k::KEY_RSHIFT   ) K.m_KeyWasDown[K.m_KeyWasDownIndex][(int)k::KEY_LSHIFT  ] = true;
                    if( Key == k::KEY_RALT     ) K.m_KeyWasDown[K.m_KeyWasDownIndex][(int)k::KEY_LALT    ] = true;
                }
            }
            break;

        case x::FocusIn:  W.m_isFocused = true;  break;
        case x::FocusOut:
            // The key releases happen in whichever window gets the focus: do not leave keys stuck down
            W.m_isFocused = false;
            K.m_KeyIsDown.fill(false);
            K.m_KeyPhysical.fill(false);
            K.m_MostRecentChar = 0;
            K.m_MostRecentKey  = xgpu::keyboard::digital::KEY_NULL;
            break;

        case x::ConfigureNotify:
            if( E.xconfigure.width > 0 && E.xconfigure.height > 0
             && ( E.xconfigure.width != W.m_Width || E.xconfigure.height != W.m_Height ) )
            {
                W.m_Width     = E.xconfigure.width;
                W.m_Height    = E.xconfigure.height;
                W.m_isResized = true;
            }
            break;

        // Vulkan can not have a swapchain with a 0x0 size: the iconified window is "minimized"
        case x::UnmapNotify: W.m_isMinimize = true;  break;
        case x::MapNotify:   W.m_isMinimize = false; break;
        }
        return true;
    }

    //----------------------------------------------------------------------------------

    bool instance::ProcessInputEvents( void ) noexcept
    {
        //
        // Clear all input data before processing
        //
        m_Mouse->m_ButtonIndex = 1 - m_Mouse->m_ButtonIndex;
        m_Mouse->m_ButtonWasDown[m_Mouse->m_ButtonIndex].fill(false);

        m_Mouse->m_Analog[(int)xgpu::mouse::analog::POS_REL  ] = { 0, 0 };
        m_Mouse->m_Analog[(int)xgpu::mouse::analog::WHEEL_REL] = { 0, 0 };

        m_Keyboard->m_KeyWasDownIndex = 1 - m_Keyboard->m_KeyWasDownIndex;
        m_Keyboard->m_KeyWasDown[m_Keyboard->m_KeyWasDownIndex].fill(false);

        //
        // Update all the window messages (no connection yet means no window yet: nothing to pump)
        //
        bool bContinue = true;
        if( m_Connection )
        {
            // Keep a reference: the loop can not lose the display even if a window goes away
            auto Connection = m_Connection;
            Display* pDisplay = Connection->m_pDisplay;
            while( XPending(pDisplay) )
            {
                XEvent Event;
                XNextEvent(pDisplay, &Event);
                if( false == Dispatch(*Connection, Event) ) bContinue = false;
            }
        }
        return bContinue;
    }
}
