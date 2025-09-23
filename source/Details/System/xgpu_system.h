#ifndef VGPU_SYSTEM_H
#define VGPU_SYSTEM_H
#pragma once
namespace xgpu::system
{
    #if defined(_WIN32)
        #include "Windows/xgpu_windows.h"
        using window    = xgpu::windows::window;
        using instance  = xgpu::windows::instance;
    #elif defined(VK_USE_PLATFORM_ANDROID_KHR)

    #endif
}
#endif
