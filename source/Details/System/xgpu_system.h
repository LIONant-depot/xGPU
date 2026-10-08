#ifndef VGPU_SYSTEM_H
#define VGPU_SYSTEM_H
#pragma once

//
// OS backend selection. Each backend provides xgpu::<os>::{instance,window} with the same interface; the
// Vulkan layer only needs to know how to turn that window into a VkSurfaceKHR (see the XGPU_SYSTEM_* checks in
// xgpu_vulkan_instance.cpp and xgpu_vulkan_window.cpp - add one #elif there per new backend).
//
//   Windows  : Win32                                  (_WIN32)
//   Linux    : X11 through Xlib, also WSLg/XWayland   (XGPU_SYSTEM_XLIB, opt in from the build; needs libX11 at link time)
//   others   : null backend, headless only            (the default off Windows; window creation fails cleanly)
//
// The window system is never picked by sniffing the machine's headers: a build that wants X11 windows defines
// XGPU_SYSTEM_XLIB (see Build/linux/CMakeLists.txt), every other non-Windows build stays windowless.
//
#if !defined(_WIN32) && !defined(VK_USE_PLATFORM_ANDROID_KHR)
    #if defined(XGPU_SYSTEM_XLIB) && defined(__linux__)
        #include "Xlib/xgpu_xlib.h"
    #else
        #ifndef XGPU_SYSTEM_NULL
            #define XGPU_SYSTEM_NULL
        #endif
        #include "Null/xgpu_null_system.h"
    #endif
#endif

namespace xgpu::system
{
    #if defined(_WIN32)
        #include "Windows/xgpu_windows.h"
        using window    = xgpu::windows::window;
        using instance  = xgpu::windows::instance;
    #elif defined(XGPU_SYSTEM_XLIB)
        using window    = xgpu::xlib::window;
        using instance  = xgpu::xlib::instance;
    #elif defined(XGPU_SYSTEM_NULL)
        // No native window backend on this platform: headless
        using window    = xgpu::nullsys::window;
        using instance  = xgpu::nullsys::instance;
    #elif defined(VK_USE_PLATFORM_ANDROID_KHR)

    #endif
}
#endif
