//
// Hands-free check of the Linux X11 window backend: window + Vulkan surface, render, resize, keyboard, mouse,
// close, clean shutdown. Synthetic input is sent with XSendEvent through a second X connection, so it needs neither
// window focus nor a window manager that cooperates (WSLg included).
//
// Prints one PASS/FAIL line per check, a summary of what the Vulkan validation layer said, and exits non-zero on failure.
//
#include "source/xGPU.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <chrono>
#include <thread>
#include <functional>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace
{
    int                      g_Fails        = 0;
    int                      g_Validation   = 0;
    std::vector<std::string> g_Messages;

    void Log( const std::string_view Msg )
    {
        ++g_Validation;
        if( g_Messages.size() < 12 ) g_Messages.emplace_back(Msg);
    }

    void Check( bool b, const char* pWhat )
    {
        std::printf( "%s  %s\n", b ? "PASS" : "FAIL", pWhat );
        if( !b ) ++g_Fails;
    }

    // evdev code + 8 = X keycode
    constexpr unsigned XKeyA = 30 + 8;
    constexpr unsigned XKeyLCtrl = 29 + 8;
    constexpr unsigned XKeyUp = 103 + 8;
}

int main()
{
    //
    // Instance, device and window
    //
    xgpu::instance Instance;
    if( auto Err = xgpu::CreateInstance( Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = false, .m_pLogErrorFunc = Log, .m_pLogWarning = Log } ); Err )
    {
        std::printf("FAIL  CreateInstance: %s\n", xgpu::getErrorMsg(Err));
        return 2;
    }
    Check(true, "CreateInstance (VK_KHR_xlib_surface requested through the backend)");

    xgpu::device Device;
    if( auto Err = Instance.Create( Device ); Err )
    {
        std::printf("FAIL  Create device: %s\n", xgpu::getErrorMsg(Err));
        return 2;
    }
    Check(true, "Create device");

    xgpu::keyboard Keyboard;  (void)Instance.Create(Keyboard);
    xgpu::mouse    Mouse;     (void)Instance.Create(Mouse);

    xgpu::window Window;
    if( auto Err = Device.Create( Window, { .m_Width = 640, .m_Height = 480 } ); Err )
    {
        std::printf("FAIL  Create window: %s\n", xgpu::getErrorMsg(Err));
        return 2;
    }
    Check(true, "Create window + Vulkan surface + swapchain");

    // second connection used only to play the part of the user and the window manager
    Display* pX = XOpenDisplay(nullptr);
    if( !pX ) { std::printf("FAIL  second X connection\n"); return 2; }
    const ::Window XWin = static_cast<::Window>( Window.getSystemWindowHandle() );

    bool bAlive = true;
    auto Frame = [&]
    {
        bAlive = Instance.ProcessInputEvents();
        if( Window.BeginRendering() ) return;
        { auto Cmd = Window.getCmdBuffer(); (void)Cmd; }
        Window.PageFlip();
    };
    // run frames until the predicate (evaluated right after the event pump of each frame) holds, at most 3 s
    auto WaitFor = [&]( const std::function<bool()>& Pred )
    {
        const auto End = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while( std::chrono::steady_clock::now() < End )
        {
            Frame();
            if( Pred() ) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    };

    //
    // Render
    //
    for( int i = 0; i < 20; ++i ) Frame();
    Check( bAlive && Window.getWidth() > 0 && Window.getHeight() > 0, "renders frames, window has a size" );
    std::printf("      size %dx%d position %d,%d\n", Window.getWidth(), Window.getHeight(), Window.getPosition().first, Window.getPosition().second );

    // The pixels really come out: capture the back buffer of a cleared frame (default clear colour 0.45 grey = 0x73)
    {
        std::vector<std::uint32_t> Pixels; int W = 0, H = 0;
        bool bAsked = false;
        for( int i = 0; i < 5 && !bAsked; ++i )
        {
            bAsked = Instance.ProcessInputEvents() && !Window.BeginRendering() && Window.Screenshot( Pixels, W, H );
            { auto Cmd = Window.getCmdBuffer(); (void)Cmd; }
            Window.PageFlip();
        }
        const std::uint32_t Mid = Pixels.empty() ? 0 : Pixels[ (std::size_t(H)/2) * W + W/2 ];
        std::printf("      centre pixel 0x%08X (%dx%d)\n", Mid, W, H );
        Check( bAsked && !Pixels.empty() && (Mid & 0xffffff) == 0x737373, "a cleared frame reaches the back buffer with the clear colour" );
    }

    //
    // Resize (from the app, and from the outside as a user dragging the border would)
    //
    Window.setSize( 800, 600 );
    Check( WaitFor([&]{ return Window.getWidth() == 800 && Window.getHeight() == 600; }), "setSize(800,600) reaches the window (ConfigureNotify)" );
    for( int i = 0; i < 10; ++i ) Frame();
    XResizeWindow( pX, XWin, 500, 400 ); XFlush(pX);
    Check( WaitFor([&]{ return Window.getWidth() == 500 && Window.getHeight() == 400; }), "external resize to 500x400 is seen and the swapchain follows" );
    for( int i = 0; i < 10; ++i ) Frame();
    Check( bAlive, "still alive after resizes" );

    //
    // Keyboard
    //
    auto SendKey = [&]( int Type, unsigned KeyCode, unsigned State = 0 )
    {
        XKeyEvent E{};
        E.type = Type; E.display = pX; E.window = XWin; E.root = DefaultRootWindow(pX); E.x = E.y = 10; E.state = State;
        E.keycode = KeyCode; E.same_screen = 1; E.time = 0;
        XSendEvent( pX, XWin, 1, Type == KeyPress ? KeyPressMask : KeyReleaseMask, reinterpret_cast<XEvent*>(&E) );
        XFlush(pX);
    };
    SendKey( KeyPress, XKeyA );
    Check( WaitFor([&]{ return Keyboard.isPressed(xgpu::keyboard::digital::KEY_A); }), "key A down" );
    Check( Keyboard.getLatestChar() == 'a', "key A gives the character 'a'" );
    SendKey( KeyRelease, XKeyA );
    Check( WaitFor([&]{ return Keyboard.wasPressed(xgpu::keyboard::digital::KEY_A); }), "key A released (wasPressed)" );
    Check( !Keyboard.isPressed(xgpu::keyboard::digital::KEY_A), "key A no longer down" );
    SendKey( KeyPress, XKeyLCtrl );
    Check( WaitFor([&]{ return Keyboard.isPressed(xgpu::keyboard::digital::KEY_LCONTROL); }), "left control down" );
    SendKey( KeyRelease, XKeyLCtrl );
    WaitFor([&]{ return Keyboard.wasPressed(xgpu::keyboard::digital::KEY_LCONTROL); });
    SendKey( KeyPress, XKeyUp );
    Check( WaitFor([&]{ return Keyboard.isPressed(xgpu::keyboard::digital::KEY_UP); }), "extended key (arrow up) maps to KEY_UP" );
    SendKey( KeyRelease, XKeyUp );
    WaitFor([&]{ return Keyboard.wasPressed(xgpu::keyboard::digital::KEY_UP); });

    //
    // Mouse
    //
    auto SendMotion = [&]( int x, int y )
    {
        XMotionEvent E{};
        E.type = MotionNotify; E.display = pX; E.window = XWin; E.root = DefaultRootWindow(pX); E.x = x; E.y = y; E.same_screen = 1;
        XSendEvent( pX, XWin, 1, PointerMotionMask, reinterpret_cast<XEvent*>(&E) );
        XFlush(pX);
    };
    auto SendButton = [&]( int Type, unsigned Button )
    {
        XButtonEvent E{};
        E.type = Type; E.display = pX; E.window = XWin; E.root = DefaultRootWindow(pX); E.x = 100; E.y = 50; E.button = Button; E.same_screen = 1;
        XSendEvent( pX, XWin, 1, Type == ButtonPress ? ButtonPressMask : ButtonReleaseMask, reinterpret_cast<XEvent*>(&E) );
        XFlush(pX);
    };
    SendMotion( 100, 50 );
    Check( WaitFor([&]{ auto p = Mouse.getValue(xgpu::mouse::analog::POS_ABS); return p[0] == 100.f && p[1] == 50.f; }), "mouse motion gives POS_ABS" );
    SendMotion( 130, 70 );
    Check( WaitFor([&]{ auto r = Mouse.getValue(xgpu::mouse::analog::POS_REL); return r[0] == 30.f && r[1] == 20.f; }), "mouse motion gives POS_REL (delta)" );
    SendButton( ButtonPress, 1 );
    Check( WaitFor([&]{ return Mouse.isPressed(xgpu::mouse::digital::BTN_LEFT); }), "left button down" );
    Check( Window.isCapturing(), "button held = window captures the pointer" );
    SendButton( ButtonRelease, 1 );
    Check( WaitFor([&]{ return Mouse.wasPressed(xgpu::mouse::digital::BTN_LEFT); }), "left button released (wasPressed)" );
    SendButton( ButtonPress, 3 );
    Check( WaitFor([&]{ return Mouse.isPressed(xgpu::mouse::digital::BTN_RIGHT); }), "right button down" );
    SendButton( ButtonRelease, 3 );
    WaitFor([&]{ return Mouse.wasPressed(xgpu::mouse::digital::BTN_RIGHT); });
    SendButton( ButtonPress, 4 );
    Check( WaitFor([&]{ return Mouse.getValue(xgpu::mouse::analog::WHEEL_REL)[0] > 0.f; }), "wheel up gives WHEEL_REL > 0" );
    SendButton( ButtonPress, 5 );
    Check( WaitFor([&]{ return Mouse.getValue(xgpu::mouse::analog::WHEEL_REL)[0] < 0.f; }), "wheel down gives WHEEL_REL < 0" );

    //
    // Close button: the window manager sends WM_DELETE_WINDOW, the loop must end
    //
    {
        XClientMessageEvent E{};
        E.type = ClientMessage; E.display = pX; E.window = XWin; E.format = 32;
        E.message_type = XInternAtom( pX, "WM_PROTOCOLS", 0 );
        E.data.l[0]    = static_cast<long>( XInternAtom( pX, "WM_DELETE_WINDOW", 0 ) );
        XSendEvent( pX, XWin, 0, 0, reinterpret_cast<XEvent*>(&E) );
        XFlush(pX);
    }
    WaitFor([&]{ return !bAlive; });
    Check( !bAlive, "close request ends the loop (ProcessInputEvents returns false)" );

    //
    // Shut down in the order an application does
    //
    XCloseDisplay(pX);
    Window = {};
    Keyboard = {};
    Mouse = {};
    Device = {};
    Instance = {};
    Check(true, "window, device and instance released without crashing");

    std::printf("\nVulkan validation/log messages: %d\n", g_Validation);
    for( auto& M : g_Messages ) std::printf("  | %s\n", M.c_str());
    std::printf("%s (%d failed)\n", g_Fails ? "SMOKE FAILED" : "SMOKE PASSED", g_Fails);
    return g_Fails ? 1 : 0;
}
