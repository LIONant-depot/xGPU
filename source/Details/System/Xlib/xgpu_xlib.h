#ifndef VGPU_XLIB_H
#define VGPU_XLIB_H
#pragma once
//
// Linux X11 window backend for xGPU, using Xlib and VK_KHR_xlib_surface (not XCB). Selected in xgpu_system.h when
// <X11/Xlib.h> is available. It also runs on WSLg and any Wayland desktop that ships XWayland.
//
// Only the window/event code is X11; the Linux OS services it shares with a future Wayland backend (thread local
// storage, the keyboard/mouse state, the evdev key table) live in ../Linux/.
//
// This header deliberately never includes <X11/Xlib.h>: Xlib #defines None/Bool/Status/Success/... which
// break unrelated code. Everything that touches Xlib lives in xgpu_xlib.cpp (which #undefs them again),
// so the window id is carried here as unsigned long.
//
#include <vulkan/vulkan.h>      // core only: no platform macro, so it does not pull Xlib in
#include <array>
#include <memory>
#include <span>
#include <cstdint>

#include "../Linux/xgpu_linux_localstorage.h"
#include "../Linux/xgpu_linux_input.h"

namespace xgpu::xlib
{
    struct connection;      // owns the Display* (defined in xgpu_xlib.cpp), shared by the instance and all its windows

    using local_storage = xgpu::linux_os::local_storage;
    using keyboard      = xgpu::linux_os::keyboard;
    using mouse         = xgpu::linux_os::mouse;

    struct instance : xgpu::details::instance_handle
    {
        // Vulkan instance extensions this backend's windows need to make a surface. Asked before the VkInstance exists
        // (they are enabled at vkCreateInstance); empty when the loader can not do X11 surfaces (headless box).
        static std::span<const char* const> getSurfaceExtensions( void ) noexcept;

        // Pumps the X event queue; false once a window was asked to close (the WM_QUIT of this backend)
        bool                            ProcessInputEvents  ( void ) noexcept;

        // The X connection, opened on first use. Null when there is no X server (no DISPLAY): instances still
        // work headless, only creating a window fails.
        std::shared_ptr<connection>     getConnection       ( void ) noexcept;

        local_storage                   m_LocalStorage      {};
        std::shared_ptr<keyboard>       m_Keyboard          = std::make_shared<keyboard>();
        std::shared_ptr<mouse>          m_Mouse             = std::make_shared<mouse>();
        std::shared_ptr<connection>     m_Connection        {};
        bool                            m_bConnectionTried  { false };
    };

    struct window : xgpu::details::window_handle
    {
        xgpu::device::error*        Initialize              ( const xgpu::window::setup& Setup, instance& Instance ) noexcept;

        virtual int                 getWidth                ( void ) const noexcept override { return m_Width; }
        virtual int                 getHeight               ( void ) const noexcept override { return m_Height; }
        virtual bool                BegingRendering         ( void )       noexcept override { return m_isMinimize; }
        virtual std::size_t         getSystemWindowHandle   ( void ) const noexcept override { return static_cast<std::size_t>(m_Window); }
        virtual bool                isFocused               ( void ) const noexcept override { return m_isFocused; }
        virtual bool                isCapturing             ( void ) const noexcept override { return m_ButtonsDown > 0; }  // X grabs the pointer implicitly while a button is held
        virtual bool                isHovered               ( void ) const noexcept override { return m_isHovered; }
        virtual void                setFocus                ( void ) const noexcept override;
        virtual std::pair<int, int> getPosition             ( void ) const noexcept override;
        virtual void                setPosition             ( int x, int y )      noexcept override;
        void                        RefreshPosition         ( void )              noexcept;   // m_TruePosition from the server (once a frame)
        virtual void                setSize                 ( int Width, int Height ) noexcept override;
        bool                        getResizedAndReset      ( void )              noexcept { auto b = m_isResized; m_isResized = false; return b; }
        bool                        isMinimized             ( void ) const noexcept override { return m_isMinimize; }
        void                        setMousePosition        ( int x, int y )      noexcept override;
        void                        setFrameless            ( bool frameless )    noexcept { m_isFrameless = frameless; }

        // Creates the VK_KHR_xlib_surface of this window (the instance must have been created with getSurfaceExtensions())
        VkResult                    CreateVulkanSurface     ( VkInstance Instance, VkSurfaceKHR& Surface ) const noexcept;

        virtual                    ~window                  ( void ) noexcept;

        std::shared_ptr<connection>                 m_Connection;   // keeps the Display alive until the window is gone
        unsigned long                               m_Window        { 0 };
        std::shared_ptr<keyboard>                   m_Keyboard;
        std::shared_ptr<mouse>                      m_Mouse;
        int                                         m_Width         { 0 };
        int                                         m_Height        { 0 };
        int                                         m_ButtonsDown   { 0 };
        bool                                        m_isMinimize    { false };
        bool                                        m_isResized     { false };
        bool                                        m_isFrameless   { false };
        bool                                        m_isHovered     { false };
        bool                                        m_isFocused     { false };
        std::pair<int, int>                         m_TruePosition  {};   // the client area's screen position (RefreshPosition)
    };
}
#endif
