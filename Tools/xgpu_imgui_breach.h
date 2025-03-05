#ifndef XGPU_IMGUI_BREACH_H
#define XGPU_IMGUI_BREACH_H
#pragma once
#include "imgui.h"

namespace xgpu::tools::imgui
{
    xgpu::device::error*    CreateContext   ( void ) noexcept;
    xgpu::device::error*    CreateInstance  ( xgpu::window& MainWindow ) noexcept;
    bool                    BeginRendering  ( bool bEnableDocking = false ) noexcept;
    bool                    isInputSleeping ( void ) noexcept;
    void                    Render          ( void ) noexcept;
    void                    Shutdown        ( void ) noexcept;
    ImFont*&                getFont         ( int Index=0) noexcept;
}
#endif // XGPU_IMGUI_BREACH_H