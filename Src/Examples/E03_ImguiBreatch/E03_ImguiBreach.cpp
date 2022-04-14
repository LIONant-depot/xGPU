#include "xGPU.h"
#include "xcore.h"
#include "../../tools/xgpu_imgui_breach.h"

namespace e03
{
    //------------------------------------------------------------------------------------------------
    static
    void DebugMessage(std::string_view View)
    {
        printf("%s\n", View.data());
    }
}

//------------------------------------------------------------------------------------------------

int E03_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = e03::DebugMessage, .m_pLogWarning = e03::DebugMessage }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);

    //
    // Setup ImGui
    //
    xgpu::tools::imgui::CreateInstance( MainWindow );

    //
    // Main loop
    //
    while (Instance.ProcessInputEvents())
    {
        if( xgpu::tools::imgui::BeginRendering() )
            continue;

        //
        // Show ImGui demo
        // 
        static bool show_demo_window = true;
        ImGui::ShowDemoWindow(&show_demo_window);

        //
        // Render
        //
        xgpu::tools::imgui::Render();

        //
        // Pageflip the windows
        //
        MainWindow.PageFlip();
    }

    return 0;
}

