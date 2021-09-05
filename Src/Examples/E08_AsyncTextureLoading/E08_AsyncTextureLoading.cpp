#include "xGPU.h"
#include "xcore.h"
#include "../../tools/xgpu_imgui_breach.h"
#include "../../tools/xgpu_xcore_bitmap_helpers.h"
#include "../../dependencies/xbmp_tools/src/xbmp_tools.h"

//------------------------------------------------------------------------------------------------
static
void DebugMessage(std::string_view View)
{
    printf("%s\n", View.data());
}

//------------------------------------------------------------------------------------------------

enum class thread_state
{ NOT_INITIALIZED
, WORKING
, EXITING
};

//------------------------------------------------------------------------------------------------

int E08_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = DebugMessage, .m_pLogWarning = DebugMessage }); Err)
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
    xgpu::tools::imgui::CreateInstance(Instance, Device, MainWindow);

    std::vector<xgpu::texture>                  LinearLoadedTextures(1000);
    std::array<std::atomic<thread_state>, 5>    TheadState{};
    std::vector<std::thread>                    WorkerThreads(TheadState.size());
    int                                         iNextFree = 0;
    float                                       Time = 0;
    auto                                        T1   = std::chrono::high_resolution_clock::now();

    //
    // Main loop
    //
    while (Instance.ProcessInputEvents())
    {
        if (xgpu::tools::imgui::BeginRendering(false))
            continue;

        //
        // Show ImGui demo
        // 
        //static bool show_demo_window = true;
        //ImGui::ShowDemoWindow(&show_demo_window);


        static ImGuiWindowFlags WindowFlags = 0;
        ImGui::SetNextWindowSize(ImVec2(650, 680));
        ImGui::Begin("Stats", nullptr, WindowFlags );

        ImGui::ProgressBar(iNextFree / (float)LinearLoadedTextures.size(), ImVec2(0.0f, 0.0f));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::Text("(%2.2fsec) Main Queue Loading", Time );

        ImGui::ProgressBar(0, ImVec2(0.0f, 0.0f));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::Text("(%2.2fsec) Async Queue Loading", 0);

        if (iNextFree < LinearLoadedTextures.size() )
        {
            for( int i=0; i< TheadState.size(); ++i )
            {
                if(iNextFree < LinearLoadedTextures.size() && TheadState[i].load() != thread_state::WORKING )
                {
                    // make sure is safe to do staff with this thread
                    if( TheadState[i].load() == thread_state::EXITING ) WorkerThreads[i].join();
                    TheadState[i].store(thread_state::WORKING);

                    WorkerThreads[i] = std::thread( 
                    [ &ThreadState = TheadState[i]
                    , &Texture    = LinearLoadedTextures[iNextFree++]
                    , &Device 
                    ]
                    {
                        xcore::bitmap Bitmap;

                        if (auto Err = xbmp::tools::loader::LoadDSS(Bitmap, "../../Src/Examples/E05_Textures/Alita-FullColor-Mipmaps.dds"); Err)
                        {
                            DebugMessage(xbmp::tools::getErrorMsg(Err));
                            std::exit(xbmp::tools::getErrorInt(Err));
                        }

                        if (auto Err = xgpu::tools::bitmap::Create(Texture, Device, Bitmap); Err)
                        {
                            DebugMessage(xgpu::getErrorMsg(Err));
                            std::exit(xgpu::getErrorInt(Err));
                        }

                       ThreadState.store(thread_state::EXITING);
                    });
                }
            }
        }

        if(iNextFree <= LinearLoadedTextures.size() ) 
        {
            if(iNextFree == LinearLoadedTextures.size()) 
            {
                for (int i = 0; i < TheadState.size(); ++i) WorkerThreads[i].join();
                iNextFree = static_cast<int>(LinearLoadedTextures.size() * 2);
            }
            Time = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - T1).count();
        }


        ImGui::End();


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

