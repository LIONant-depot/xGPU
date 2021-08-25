#include "imgui.h"
namespace xgpu::tools::imgui
{
    xgpu::device::error*    CreateInstance  ( xgpu::instance& Intance, xgpu::device& Device, xgpu::window& MainWindow ) noexcept;
    bool                    isMinimize      ( void ) noexcept;
    void                    Render          ( void ) noexcept;
    void                    Shutdown        ( void ) noexcept;
}