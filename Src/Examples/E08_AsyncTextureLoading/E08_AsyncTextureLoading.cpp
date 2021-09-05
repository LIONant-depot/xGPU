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

struct stinfo
{
    float m_Time;
    float m_Percentage;
};

constexpr auto g_StatName = std::array
{ "MainQ + Disk Loading"
, "MainQ + Cache"
, "AsyncQ + Cache"
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

    std::vector<xgpu::texture>              LoadedTextures; 
    constexpr auto                          n_textures      = 1000;
    constexpr auto                          n_max_workers   = 20;
    std::array<stinfo, g_StatName.size()>   Stats            {};
    auto                                    T1              = std::chrono::high_resolution_clock::now();
    xcore::scheduler::channel               Channel         { xconst_universal_str("LoadingTextures") };
    std::atomic<int>                        WorkerBusy      {0};
    xcore::bitmap                           Bitmap;
    int                                     iActiveStat     {0};

    //
    // Make it grow!
    //
    LoadedTextures.reserve(n_textures);

    //
    // Load the texture ones since we don't want to measure how long it takes to load from disk
    //
    if (auto Err = xbmp::tools::loader::LoadDSS(Bitmap, "../../Src/Examples/E05_Textures/Alita-FullColor-Mipmaps.dds"); Err)
    {
        DebugMessage(xbmp::tools::getErrorMsg(Err));
        std::exit(xbmp::tools::getErrorInt(Err));
    }

    //
    // Main loop
    //
    while (Instance.ProcessInputEvents())
    {
        if (xgpu::tools::imgui::BeginRendering(false))
            continue;

        //
        // Create an ImGui window and display progress bar info
        // 
        static ImGuiWindowFlags WindowFlags = 0;
        ImGui::SetNextWindowSize(ImVec2(650, 680));
        ImGui::Begin("Stats", nullptr, WindowFlags );

        for( int i=0; i< g_StatName.size(); ++i )
        {
            ImGui::ProgressBar(Stats[i].m_Percentage, ImVec2(0.0f, 0.0f) );
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::Text("(%2.2fsec) %s", Stats[i].m_Time, g_StatName[i]);
        }

        // End of window
        ImGui::End();

        //
        // Handle input
        //
        static bool StartProfiling = false;
        if( StartProfiling == false && ImGui::GetIO().KeysDown[(int)xgpu::keyboard::digital::KEY_SPACE] ) StartProfiling = true;

        //
        // Process the textures
        //
        while(iActiveStat < (int)g_StatName.size() && StartProfiling && LoadedTextures.size() < n_textures )
        {
            // Start the clock
            if(LoadedTextures.size() ==0) T1 = std::chrono::high_resolution_clock::now();

            if( auto nWorkers = WorkerBusy.load(std::memory_order_relaxed); nWorkers >= (n_max_workers-1) ) 
                break;
            else if( WorkerBusy.compare_exchange_weak(nWorkers, nWorkers + 1) )
            {
                LoadedTextures.emplace_back();

                switch ( iActiveStat )
                {
                case 0: Channel.SubmitJob( 
                        [ &WorkerBusy
                        , &Device
                        , &Texture = LoadedTextures.back()
                        ]
                        {
                            xcore::bitmap Bitmap;

                            //
                            // Load the texture ones since we don't want to measure how long it takes to load from disk
                            //
                            if (auto Err = xbmp::tools::loader::LoadDSS(Bitmap, "../../Src/Examples/E05_Textures/Alita-FullColor-Mipmaps.dds"); Err)
                            {
                                DebugMessage(xbmp::tools::getErrorMsg(Err));
                                std::exit(xbmp::tools::getErrorInt(Err));
                            }

                            //
                            // Load to gpu
                            //
                            if (auto Err = xgpu::tools::bitmap::Create(Texture, Device, Bitmap); Err)
                            {
                                DebugMessage(xgpu::getErrorMsg(Err));
                                std::exit(xgpu::getErrorInt(Err));
                            }

                            WorkerBusy--;
                        });
                        break;
                case 1: Channel.SubmitJob( 
                        [ &WorkerBusy
                        , &Device
                        , &Texture = LoadedTextures.back()
                        , &Bitmap
                        ]
                        {
                            //
                            // Load to gpu
                            //
                            if (auto Err = xgpu::tools::bitmap::Create(Texture, Device, Bitmap); Err)
                            {
                                DebugMessage(xgpu::getErrorMsg(Err));
                                std::exit(xgpu::getErrorInt(Err));
                            }

                            WorkerBusy--;
                        });
                        break;
                }
            }
        }

        // 
        // Measure final time
        //
        if( iActiveStat < (int)g_StatName.size() && LoadedTextures.size() && LoadedTextures.size() <= n_textures )
        {
            if( LoadedTextures.size() == n_textures )
            {
                // wait for all the workers to be done
                Channel.join();
                assert(WorkerBusy == 0);
                Stats[iActiveStat].m_Time       = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - T1).count();
                Stats[iActiveStat].m_Percentage = 1;
                LoadedTextures.clear();
                StartProfiling = false;
                iActiveStat++;
            }
            else 
            {
                Stats[iActiveStat].m_Time       = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - T1).count();
                Stats[iActiveStat].m_Percentage = LoadedTextures.size() / (float)n_textures;
            }
        }

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

