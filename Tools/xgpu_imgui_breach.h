#include "imgui.h"
namespace xgpu::tools::imgui
{
    xgpu::device::error*    CreateInstance  ( xgpu::instance& Intance, xgpu::device& Device, xgpu::window& MainWindow ) noexcept;
    bool                    BeginRendering  ( bool bEnableDocking = false ) noexcept;
    void                    Render          ( void ) noexcept;
    void                    Shutdown        ( void ) noexcept;

}