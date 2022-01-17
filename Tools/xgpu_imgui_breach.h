#include "imgui.h"
namespace xgpu::tools::imgui
{
    xgpu::device::error*    CreateContext   ( void ) noexcept;
    xgpu::device::error*    CreateInstance  ( xgpu::window& MainWindow ) noexcept;
    bool                    BeginRendering  ( bool bEnableDocking = false ) noexcept;
    bool                    isInputSleeping ( void ) noexcept;
    void                    Render          ( void ) noexcept;
    void                    Shutdown        ( void ) noexcept;

}