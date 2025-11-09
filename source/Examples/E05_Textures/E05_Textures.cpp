#define IMGUI_DEFINE_MATH_OPERATORS // Allows ImVec2 arithmetic

#include "E05_BitmapInspector.h"
#include "source/tools/xgpu_imgui_breach.h"
#include "dependencies/xmath/source/xmath.h"
#include "dependencies/imgui/imgui_internal.h"
#include "source/Tools/xgpu_xcore_bitmap_helpers.h"
#include <format>

constexpr auto g_VertShaderSPV = std::array
{
    #include "imgui_vert.h"
};
constexpr auto g_FragShaderSPV = std::array
{
    #include "draw_frag.h"
};

namespace e05
{
    //------------------------------------------------------------------------------------------------

    struct vert_2d
    {
        float           m_X, m_Y;
        float           m_U, m_V;
        std::uint32_t   m_Color;
    };

    //------------------------------------------------------------------------------------------------

    struct push_contants
    {
        xmath::fvec2 m_Scale;
        xmath::fvec2 m_Translation;
        xmath::fvec2 m_UVScale;
    };
}

//------------------------------------------------------------------------------------------------

int E05_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = e05::DebugMessage, .m_pLogWarning = e05::DebugMessage }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);

    //
    // Define a material
    //
    xgpu::pipeline Pipeline;
    {
        xgpu::vertex_descriptor VertexDescriptor;
        {
            auto Attributes = std::array
            {
                xgpu::vertex_descriptor::attribute
                {
                    .m_Offset = offsetof(e05::vert_2d, m_X)
                ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
                }
            ,   xgpu::vertex_descriptor::attribute
                {
                    .m_Offset = offsetof(e05::vert_2d, m_U)
                ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
                }
            ,   xgpu::vertex_descriptor::attribute
                {
                    .m_Offset = offsetof(e05::vert_2d, m_Color)
                ,   .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED
                }
            };
            auto Setup = xgpu::vertex_descriptor::setup
            {
                .m_VertexSize = sizeof(e05::vert_2d)
            ,   .m_Attributes = Attributes
            };

            if (auto Err = Device.Create(VertexDescriptor, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader MyFragmentShader;
        {
            xgpu::shader::setup Setup
            { .m_Type = xgpu::shader::type::bit::FRAGMENT
            , .m_Sharer = xgpu::shader::setup::raw_data{ g_FragShaderSPV }
            };
            if (auto Err = Device.Create(MyFragmentShader, Setup ); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader MyVertexShader;
        {
            xgpu::shader::setup Setup
            {
                .m_Type                 = xgpu::shader::type::bit::VERTEX
            ,   .m_Sharer               = xgpu::shader::setup::raw_data{g_VertShaderSPV}
            };

            if (auto Err = Device.Create(MyVertexShader, Setup ); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Shaders    = std::array<const xgpu::shader*, 2>{ &MyFragmentShader, &MyVertexShader };
        auto Samplers   = std::array{ xgpu::pipeline::sampler{} };
        auto Setup      = xgpu::pipeline::setup
        {
            .m_VertexDescriptor  = VertexDescriptor
        ,   .m_Shaders           = Shaders
        ,   .m_PushConstantsSize = sizeof(e05::push_contants)
        ,   .m_Samplers          = Samplers
        ,   .m_DepthStencil      = { .m_bDepthTestEnable = false }
        };

        if (auto Err = Device.Create(Pipeline, Setup ); Err)
            return xgpu::getErrorInt(Err);
    }

    xgpu::buffer VertexBuffer;
    {
        if (auto Err = Device.Create(VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(e05::vert_2d), .m_EntryCount = 4 }); Err)
            return xgpu::getErrorInt(Err);

        (void)VertexBuffer.MemoryMap( 0, 4, [&](void* pData)
        {
            auto pVertex   = static_cast<e05::vert_2d*>(pData);
            pVertex[0]  = { -100.0f, -100.0f,  0.0f, 0.0f, 0xffffffff };
            pVertex[1]  = {  100.0f, -100.0f,  1.0f, 0.0f, 0xffffffff };
            pVertex[2]  = {  100.0f,  100.0f,  1.0f, 1.0f, 0xffffffff };
            pVertex[3]  = { -100.0f,  100.0f,  0.0f, 1.0f, 0xffffffff };
        });
    }

    xgpu::buffer IndexBuffer;
    {
        if (auto Err = Device.Create(IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = 6 }); Err)
            return xgpu::getErrorInt(Err);

        (void)IndexBuffer.MemoryMap( 0, 6, [&](void* pData)
        {
            auto            pIndex      = static_cast<std::uint32_t*>(pData);
            constexpr auto  StaticIndex = std::array
            {
                2u,  1u,  0u,      3u,  2u,  0u,    // front
            };
            static_assert(StaticIndex.size() == 6);
            for( auto i : StaticIndex )
            {
                *pIndex = i;
                pIndex++;
            }
        });
    }


    //
    // Setup ImGui
    //
    xgpu::tools::imgui::CreateInstance(MainWindow);

    //
    // Load bitmaps
    //
    constexpr auto TextureList = std::array
    { L"../../source/Examples/E05_Textures/Alita-FullColor.dds"
    , L"../../source/Examples/E05_Textures/Alita-FullColor-Mipmaps.dds"
    , L"../../source/Examples/E05_Textures/Alita-FullColorNoAlpha-Mipmaps.dds"
    , L"../../source/Examples/E05_Textures/Alita-DXT1-Mipmaps.dds"
    , L"../../source/Examples/E05_Textures/Alita-DXT1-NoAlpha-Mipmaps.dds"
    , L"../../source/Examples/E05_Textures/Alita-DXT3.dds"
    , L"../../source/Examples/E05_Textures/Alita-DXT5.dds"
  //  , L"../../bin/Run3.basis"
    };

    constexpr auto TextureListForBombo = std::array
    { "../../source/Examples/E05_Textures/Alita-FullColor.dds"
    , "../../source/Examples/E05_Textures/Alita-FullColor-Mipmaps.dds"
    , "../../source/Examples/E05_Textures/Alita-FullColorNoAlpha-Mipmaps.dds"
    , "../../source/Examples/E05_Textures/Alita-DXT1-Mipmaps.dds"
    , "../../source/Examples/E05_Textures/Alita-DXT1-NoAlpha-Mipmaps.dds"
    , "../../source/Examples/E05_Textures/Alita-DXT3.dds"
    , "../../source/Examples/E05_Textures/Alita-DXT5.dds"
        //  , ../../bin/Run3.basis"
    };

    std::array<xgpu::texture,           TextureList.size()>     GPUTextureList;
    std::array<e05::bitmap_inspector,   TextureList.size()>     BitmapInspector;
    std::array<xgpu::pipeline_instance, TextureList.size()>     PipelineInstance;
    for( int i=0; i< TextureList.size(); ++i )
    {
        auto& BitmapInspect = BitmapInspector[i];
        auto& PipeInst      = PipelineInstance[i];
        auto& TextureName   = TextureList[i];

        BitmapInspect.Load(TextureName);

        //
        // Create Pipeline Instance
        //
        {
            xgpu::texture&   Texture = GPUTextureList[i];

            //
            // Create the texture
            //
            if (auto Err = xgpu::tools::bitmap::Create(Texture, Device, *BitmapInspect.m_pBitmap ); Err)
            {
                e05::DebugMessage(xgpu::getErrorMsg(Err));
                std::exit(xgpu::getErrorInt(Err));
            }

            //
            // Create the pipeline
            //
            auto            Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ Texture } };
            auto            Setup    = xgpu::pipeline_instance::setup
            { .m_PipeLine         = Pipeline
            , .m_SamplersBindings = Bindings
            };

            if (auto Err = Device.Create(PipeInst, Setup); Err)
            {
                e05::DebugMessage(xgpu::getErrorMsg(Err));
                std::exit(xgpu::getErrorInt(Err));
            }
        }
    }

    //
    // Create the background material
    //
    xgpu::pipeline_instance BackgroundMaterialInstance;
    {
        xgpu::texture Texture;
        if (auto Err = xgpu::tools::bitmap::Create(Texture, Device, xbitmap::getDefaultBitmap() ); Err)
        {
            e05::DebugMessage(xgpu::getErrorMsg(Err));
            std::exit(xgpu::getErrorInt(Err));
        }

        auto  Bindings  = std::array{ xgpu::pipeline_instance::sampler_binding{ Texture } };
        auto  Setup     = xgpu::pipeline_instance::setup
        { .m_PipeLine           = Pipeline
        , .m_SamplersBindings   = Bindings
        };

        if (auto Err = Device.Create(BackgroundMaterialInstance, Setup); Err)
        {
            e05::DebugMessage(xgpu::getErrorMsg(Err));
            std::exit(xgpu::getErrorInt(Err));
        }
    }

    //
    // Storate some input context
    //
    int             iActiveImage = 0;
    float           MouseScale   = 1;
    xmath::fvec2  MouseTranslate(0,0);

    //
    // Create the inspector window
    //
    xproperty::inspector Inspector("Property");
    Inspector.AppendEntity();
    Inspector.AppendEntityComponent( *xproperty::getObject(BitmapInspector[iActiveImage]), &BitmapInspector[iActiveImage]);

    //
    // Main loop
    //
    while (Instance.ProcessInputEvents())
    {
        if (xgpu::tools::imgui::BeginRendering( true ))
            continue;

        //
        // Render the window
        //
        {
            //
            // Render Image
            //
            ImGui::Begin("PictureInWindow", nullptr, ImGuiWindowFlags_NoBackground);
            const bool isWindowed = ImGui::IsWindowHovered();
            float NewMouseWheel = 0;
            if (isWindowed)
            {
                auto& io = ImGui::GetIO();
                NewMouseWheel = io.MouseWheel;
            }

            xgpu::tools::imgui::AddCustomRenderCallback([&](xgpu::cmd_buffer& CmdBuffer, const ImVec2& windowPos, const ImVec2& windowSize )
            {
                //
                // Handle Input
                //
                if (isWindowed)
                {
                    const float  OldScale = MouseScale;
                    auto& io = ImGui::GetIO();

                    if (io.MouseDown[1] || io.MouseDown[2])
                    {
                        if (io.MouseDown[0])
                        {
                            MouseScale -= 8000.0f * io.DeltaTime * (io.MouseDelta.y * (2.0f / windowSize.y));
                        }
                        else
                        {
                            MouseTranslate.m_X += io.MouseDelta.x * (2.0f / windowSize.x);
                            MouseTranslate.m_Y += io.MouseDelta.y * (2.0f / windowSize.y);
                        }
                    }

                    // set the mouse pos relative to the window
                    const auto MousePosX = io.MousePos.x - windowPos.x;
                    const auto MousePosY = io.MousePos.y - windowPos.y;

                    // Wheel scale
                    MouseScale += 0.09f * NewMouseWheel;
                    if (MouseScale < 0.1f) MouseScale = 0.1f;

                    // Always zoom from the perspective of the mouse
                    const float mx = ((MousePosX / (float)windowSize.x) - 0.5f) * 2.0f;
                    const float my = ((MousePosY / (float)windowSize.y) - 0.5f) * 2.0f;
                    MouseTranslate.m_X += (MouseTranslate.m_X - mx) * (MouseScale - OldScale) / OldScale;
                    MouseTranslate.m_Y += (MouseTranslate.m_Y - my) * (MouseScale - OldScale) / OldScale;

                    // Make sure that the picture does not leave the view
                    // should be 0.5 but I left a little bit of the picture inside the view with 0.4f
                    const float BorderX = ((BitmapInspector[iActiveImage].m_pBitmap->getWidth() * 0.4f)  / (float)windowSize.x) * MouseScale;
                    const float BorderY = ((BitmapInspector[iActiveImage].m_pBitmap->getHeight() * 0.4f) / (float)windowSize.y) * MouseScale;
                    MouseTranslate.m_X = std::min(1.0f + BorderX, std::max(-1.0f - BorderX, MouseTranslate.m_X));
                    MouseTranslate.m_Y = std::min(1.0f + BorderY, std::max(-1.0f - BorderY, MouseTranslate.m_Y));
                }

                //
                // Set the view port to match the actual window size
                // 
                CmdBuffer.setViewport
                ( windowPos.x
                , windowPos.y
                , windowSize.x
                , windowSize.y
                );

                //
                // Render background
                //
                {
                    CmdBuffer.setPipelineInstance(BackgroundMaterialInstance);
                    CmdBuffer.setBuffer(VertexBuffer);
                    CmdBuffer.setBuffer(IndexBuffer);

                    e05::push_contants PushContants;

                    PushContants.m_Scale =
                    { (150 * 2.0f) / windowSize.x
                    , (150 * 2.0f) / windowSize.y
                    };
                    PushContants.m_Translation = xmath::fvec2::fromZero();
                    PushContants.m_UVScale     = { 100.0f,100.0f };

                    CmdBuffer.setPushConstants(PushContants);

                    CmdBuffer.Draw(6);
                }

                //
                // Render Image
                //
                CmdBuffer.setPipelineInstance(PipelineInstance[iActiveImage]);
                CmdBuffer.setBuffer(VertexBuffer);
                CmdBuffer.setBuffer(IndexBuffer);

                e05::push_contants PushContants;
                PushContants.m_Scale = { (MouseScale * 2.0f * windowSize.y/800) / windowSize.x * BitmapInspector[iActiveImage].m_pBitmap->getAspectRatio()
                                       , (MouseScale * 2.0f * windowSize.y/800) / windowSize.y
                                       };
                PushContants.m_UVScale.setup(1.0f, 1.0f);
                PushContants.m_Translation = MouseTranslate;

                CmdBuffer.setPushConstants( PushContants );

                CmdBuffer.Draw( 6 );
            });
            ImGui::End();
        }

        //
        // IMGUI demo
        //
        if constexpr (false)
        {
            static bool show_demo_window = true;
            ImGui::ShowDemoWindow(&show_demo_window);
        }

        //
        // Render the Inspector
        //
        xproperty::settings::context Context;
        Inspector.Show(Context, [&]
        {
            if (ImGui::Combo("Select File", &iActiveImage, TextureListForBombo.data(), static_cast<int>(TextureListForBombo.size())))
            {
                Inspector.clear();
                Inspector.AppendEntity();
                Inspector.AppendEntityComponent(*xproperty::getObject(BitmapInspector[iActiveImage]), &BitmapInspector[iActiveImage]);
            }
        });

        //
        // Render the texture into a ImGUI::Window as a grid...
        //
        ImGui::Begin("TextureInWindow");
        {
            constexpr auto  PicSize = 128;

            for (int y = 0; y < 3; ++y)
            {
                for (int x = 0; x < 3; ++x)
                {
                    const auto& bitmap = BitmapInspector[iActiveImage].m_pBitmap;
                    const float aspect = bitmap->getAspectRatio();

                    // Start a fixed-size cell
                    ImGui::BeginGroup(); 
                    ImVec2 cursorPos = ImGui::GetCursorPos();

                    ImVec2 imageSize;
                    ImVec2 offset = { 0, 0 };

                    if (aspect >= 1.0f) 
                    { // Wider than tall
                        imageSize = ImVec2(PicSize, PicSize / aspect);
                        offset.y = (PicSize - imageSize.y) * 0.5f;
                    }
                    else 
                    { // Taller than wide
                        imageSize = ImVec2(PicSize * aspect, PicSize);
                        offset.x = (PicSize - imageSize.x) * 0.5f;
                    }

                    // Apply offset to center the image
                    ImGui::SetCursorPos(cursorPos + offset);
                    ImGui::Image((void*)(intptr_t)&GPUTextureList[iActiveImage], imageSize);

                    // Move cursor to bottom-right of the 128x128 cell
                    ImGui::SetCursorPos(cursorPos + ImVec2(PicSize, PicSize));

                    // Add a Dummy item to reserve the cell space
                    // Zero-sized dummy to register the cursor position
                    ImGui::Dummy(ImVec2(0, 0)); 

                    ImGui::EndGroup();

                    if (x != 2) ImGui::SameLine();
                }
            }
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
