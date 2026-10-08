#ifndef VGPU_SYSTEM_H
#define VGPU_SYSTEM_H
#pragma once
// Linux port: null OS backend, included at global scope
#if !defined(_WIN32) && !defined(VK_USE_PLATFORM_ANDROID_KHR)
    #include "Null/xgpu_null_system.h"
#endif

namespace xgpu::system
{
    #if defined(_WIN32)
        #include "Windows/xgpu_windows.h"
        using window    = xgpu::windows::window;
        using instance  = xgpu::windows::instance;
    #elif !defined(VK_USE_PLATFORM_ANDROID_KHR)
        // Linux port: no native window backend yet, use the null backend (headless)
        using window    = xgpu::nullsys::window;
        using instance  = xgpu::nullsys::instance;
    #elif defined(VK_USE_PLATFORM_ANDROID_KHR)

    #endif
}
#endif
