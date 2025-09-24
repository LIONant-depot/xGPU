#include "source/xGPU.h"
#include "source/tools/xgpu_imgui_breach.h"
#include "source/tools/xgpu_xcore_bitmap_helpers.h"
#include "dependencies/xtexture.plugin/dependencies/xbmp_tools/src/xbmp_tools.h"
#include "dependencies/xscheduler/source/xscheduler.h"
#include <chrono>


namespace e08
{
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

    //------------------------------------------------------------------------------------------------

    constexpr auto g_StatName = std::array
    { "OpenGL + Disk Loading"
    , "OpenGL + Cache"
    , "MainQ + Disk Loading"
    , "MainQ + Cache"
    };
};

//------------------------------------------------------------------------------------------------

int E08_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = e08::DebugMessage, .m_pLogWarning = e08::DebugMessage }); Err)
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
    xgpu::tools::imgui::CreateInstance( MainWindow);

    //
    // Initialize stats
    //
    constexpr auto                          n_textures_v    = 1000;
    constexpr auto                          n_max_workers_v = 20;

    std::vector<xgpu::texture>              LoadedTextures  {}; 
    std::array<e08::stinfo, e08::g_StatName.size()>   Stats           {};
    auto                                    T0              = std::chrono::high_resolution_clock::now();
    xscheduler::task_group                  TaskGroup      { xscheduler::str_v<"LoadingTextures"> };
    std::atomic<int>                        nWorkerBusy     {0};
    xbitmap                                 CachedBitmap    {};
    int                                     iActiveStat     {0};

    //
    // Make it grow!
    //
    LoadedTextures.reserve(n_textures_v);

    //
    // Load the texture ones since we don't want to measure how long it takes to load from disk
    //
    if (auto Err = xbmp::tools::loader::LoadDSS(CachedBitmap, L"../../source/Examples/E05_Textures/Alita-FullColor-Mipmaps.dds"); Err)
    {
        assert(false);
        e08::DebugMessage(Err.getMessage());
        std::exit(Err.getStateUID());
    }

    xgpu::keyboard  Keyboard;
    {
        Instance.Create(Keyboard, {});
    }


    //
    // Main loop
    //
    while (Instance.ProcessInputEvents())
    {
        //
        // Handle input
        //
        static bool StartProfiling = false;
        if( StartProfiling == false && Keyboard.wasPressed(xgpu::keyboard::digital::KEY_SPACE)) StartProfiling = true;

        //
        // Process the textures
        //
        while(iActiveStat < (int)e08::g_StatName.size() && StartProfiling && LoadedTextures.size() < n_textures_v )
        {
            // Start the clock
            if(LoadedTextures.size() ==0) T0 = std::chrono::high_resolution_clock::now();

            if( auto nWorkers = nWorkerBusy.load(std::memory_order_relaxed); nWorkers >= (n_max_workers_v-1) ) 
                break;
            else if( nWorkerBusy.compare_exchange_weak(nWorkers, nWorkers + 1) )
            {
                constexpr auto  step_size_v = 1ull;
                const int       Index       = (int)LoadedTextures.size();
                const int       Count       = (int)std::min( n_textures_v - LoadedTextures.size(), step_size_v );
                LoadedTextures.resize(LoadedTextures.size() + Count );

                switch ( iActiveStat )
                {
                case 0: for( int i=0; i<Count; ++i )
                        {
                            xbitmap Bitmap;

                            //
                            // Load the texture ones since we don't want to measure how long it takes to load from disk
                            //
                            if (auto Err = xbmp::tools::loader::LoadDSS(Bitmap, L"../../source/Examples/E05_Textures/Alita-FullColor-Mipmaps.dds"); Err)
                            {
                                assert(false);
                                e08::DebugMessage(Err.getMessage());
                                std::exit(Err.getStateUID());
                            }

                            //
                            // Load to gpu
                            //
                            if (auto Err = xgpu::tools::bitmap::Create(LoadedTextures[Index+i], Device, Bitmap); Err)
                            {
                                assert(false);
                                e08::DebugMessage(getErrorMsg(Err) );
                                std::exit(xgpu::getErrorInt(Err));
                            }

                            nWorkerBusy.store( n_max_workers_v );
                        }
                        break;
                case 1: for( int i=0; i<Count; ++i )
                        {
                            //
                            // Load to gpu
                            //
                            if (auto Err = xgpu::tools::bitmap::Create(LoadedTextures[Index+i], Device, CachedBitmap); Err)
                            {
                                assert(false);
                                e08::DebugMessage(xgpu::getErrorMsg(Err));
                                std::exit(xgpu::getErrorInt(Err));
                            }

                            nWorkerBusy.store( n_max_workers_v );
                        }
                        break;
                case 2: TaskGroup.Submit(
                        [ &nWorkerBusy
                        , &Device
                        , &LoadedTextures
                        , Index, Count
                        ]
                        {
                            for (int i = 0; i < Count; ++i)
                            {
                                xbitmap Bitmap;

                                //
                                // Load the texture ones since we don't want to measure how long it takes to load from disk
                                //
                                if (auto Err = xbmp::tools::loader::LoadDSS(Bitmap, L"../../source/Examples/E05_Textures/Alita-FullColor-Mipmaps.dds"); Err)
                                {
                                    assert(false);
                                    e08::DebugMessage(Err.getMessage());
                                    std::exit(Err.getStateUID());
                                }

                                //
                                // Load to gpu
                                //
                                if (auto Err = xgpu::tools::bitmap::Create(LoadedTextures[Index + i], Device, Bitmap); Err)
                                {
                                    assert(false);
                                    e08::DebugMessage(xgpu::getErrorMsg(Err));
                                    std::exit(xgpu::getErrorInt(Err));
                                }
                            }

                            nWorkerBusy--;
                        });
                        break;
                case 3: TaskGroup.Submit(
                        [ &nWorkerBusy
                        , &Device
                        , &CachedBitmap
                        , &LoadedTextures
                        , Index, Count
                        ]
                        {
                            for (int i = 0; i < Count; ++i)
                            {
                                //
                                // Load to gpu
                                //
                                if (auto Err = xgpu::tools::bitmap::Create(LoadedTextures[Index+i], Device, CachedBitmap); Err)
                                {
                                    assert(false);
                                    e08::DebugMessage(xgpu::getErrorMsg(Err));
                                    std::exit(xgpu::getErrorInt(Err));
                                }
                            }

                            nWorkerBusy--;
                        });
                        break;
                }
            }
        }

        // For case 2 we have to do a bit of magic
        if(iActiveStat <= 1) nWorkerBusy.store(0);

        // 
        // Measure final time
        //
        if( iActiveStat < (int)e08::g_StatName.size() && LoadedTextures.size() && LoadedTextures.size() <= n_textures_v )
        {
            if( LoadedTextures.size() == n_textures_v )
            {
                // wait for all the workers to be done
                TaskGroup.join();
                assert(nWorkerBusy == 0);

                Stats[iActiveStat].m_Time       = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - T0).count();
                Stats[iActiveStat].m_Percentage = 1;

                LoadedTextures.clear();
                StartProfiling = false;
                iActiveStat++;
            }
            else 
            {
                Stats[iActiveStat].m_Time       = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - T0).count();
                Stats[iActiveStat].m_Percentage = LoadedTextures.size() / (float)n_textures_v;
            }
        }

        //
        // Render!
        //
        if (xgpu::tools::imgui::BeginRendering(false))
            continue;

        //
        // Create an ImGui window and display progress bar info
        // 
        ImGui::SetNextWindowSize(ImVec2(650, 680));
        ImGui::Begin("Stats", nullptr, 0);

        for (int i = 0; i < e08::g_StatName.size(); ++i)
        {
            ImGui::ProgressBar(Stats[i].m_Percentage, ImVec2(0.0f, 0.0f));
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::Text("(%2.2fsec) %s", Stats[i].m_Time, e08::g_StatName[i] );
        }

        // End of window
        ImGui::End();

        //
        // Render ImGui
        //
        xgpu::tools::imgui::Render();

        //
        // Pageflip the windows
        //
        MainWindow.PageFlip();
    }

    return 0;
}

