#include "xGPU.h"
#include "xcore.h"

#include "../../tools/xgpu_imgui_breach.h"
#include "../../tools/xgpu_xcore_bitmap_helpers.h"
#include "../../dependencies/xtexture.plugin/dependencies/xresource_pipeline_v2/dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.h"
#include "../../dependencies/xtexture.plugin/dependencies/xbmp_tools/src/xbmp_tools.h"
#include "../../tools/xgpu_basis_universal_texture_loader.h"
#include <format>

// This define forces the pipeline to ignore including the empty functions that the compiler needs to link
#define XRESOURCE_PIPELINE_NO_COMPILER
#include "../../dependencies/xtexture.plugin/dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"
#include "../../dependencies/xtexture.plugin/source/xtexture_rsc_descriptor.h"
#include "imgui_internal.h"

constexpr auto g_VertShaderSPV = std::array
{
    #include "x64/imgui_vert.h"
};
constexpr auto g_FragShaderSPV = std::array
{
    #include "x64/draw_frag.h"
};

static_assert(xproperty::meta::meets_requirements_v< true, false, std::string> );


namespace e10
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
        xcore::vector2 m_Scale;
        xcore::vector2 m_Translation;
        xcore::vector2 m_UVScale;
    };

    //------------------------------------------------------------------------------------------------

    static
        void DebugMessage(std::string_view View)
    {
        printf("%s\n", View.data());
    }

    //------------------------------------------------------------------------------------------------

    struct bitmap_inspector;

    //------------------------------------------------------------------------------------------------

    struct ivec2
    {
        std::uint32_t x, y;
        XPROPERTY_DEF
        ("ivec2", ivec2, xproperty::settings::vector2_group
            , obj_member_ro<"W", &ivec2::x >
            , obj_member_ro<"H", &ivec2::y >
        )
    };
    XPROPERTY_REG(ivec2)

}

//------------------------------------------------------------------------------------------------

struct errors
{
    std::vector<std::string> m_Errors;
    XPROPERTY_DEF
    ( "Validation", errors
    , obj_member_ro<"ErrorList", &errors::m_Errors, member_ui_open<true> >
    )
};
XPROPERTY_REG(errors)


#include "E10_Compiler.h"


//------------------------------------------------------------------------------------------------
float s_AspectRation = 1;
void LoadTexture(const std::string& ResourcePath, xgpu::texture& Texture, xgpu::device& Device, bool isSRGB)
{
    xcore::bitmap Bitmap;
    std::wcout << L"Loading Texture: " << ResourcePath.c_str() << L"\n";

    if (auto pError = xbmp::tools::loader::LoadDSS(Bitmap, ResourcePath.c_str()); pError)
    {
        e10::DebugMessage("Failed to load the resource dds");
        std::exit(-1);
    }

    if (isSRGB) Bitmap.setColorSpace(xcore::bitmap::color_space::SRGB);
    else        Bitmap.setColorSpace(xcore::bitmap::color_space::LINEAR);


    if (auto Err = xgpu::tools::bitmap::Create(Texture, Device, Bitmap); Err)
    {
        e10::DebugMessage(xgpu::getErrorMsg(Err));
        std::exit(xgpu::getErrorInt(Err));
    }

    s_AspectRation = Bitmap.getAspectRatio();
}

//------------------------------------------------------------------------------------------------

int E10_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = e10::DebugMessage, .m_pLogWarning = e10::DebugMessage }); Err)
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
                    .m_Offset = offsetof(e10::vert_2d, m_X)
                ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
                }
            ,   xgpu::vertex_descriptor::attribute
                {
                    .m_Offset = offsetof(e10::vert_2d, m_U)
                ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
                }
            ,   xgpu::vertex_descriptor::attribute
                {
                    .m_Offset = offsetof(e10::vert_2d, m_Color)
                ,   .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED
                }
            };
            auto Setup = xgpu::vertex_descriptor::setup
            {
                .m_VertexSize = sizeof(e10::vert_2d)
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
            if (auto Err = Device.Create(MyFragmentShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader MyVertexShader;
        {
            xgpu::shader::setup Setup
            {
                .m_Type = xgpu::shader::type::bit::VERTEX
            ,   .m_Sharer = xgpu::shader::setup::raw_data{g_VertShaderSPV}
            };

            if (auto Err = Device.Create(MyVertexShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Shaders = std::array<const xgpu::shader*, 2>{ &MyFragmentShader, & MyVertexShader };
        auto Samplers = std::array{ xgpu::pipeline::sampler{} };
        auto Setup = xgpu::pipeline::setup
        {
            .m_VertexDescriptor  = VertexDescriptor
        ,   .m_Shaders           = Shaders
        ,   .m_PushConstantsSize = sizeof(e10::push_contants)
        ,   .m_Samplers          = Samplers
        ,   .m_DepthStencil      = {.m_bDepthTestEnable = false }
        ,   .m_Blend             = xgpu::pipeline::blend::getAlphaOriginal()
        };

        if (auto Err = Device.Create(Pipeline, Setup); Err)
            return xgpu::getErrorInt(Err);
    }

    xgpu::buffer VertexBuffer;
    {
        if (auto Err = Device.Create(VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(e10::vert_2d), .m_EntryCount = 4 }); Err)
            return xgpu::getErrorInt(Err);

        (void)VertexBuffer.MemoryMap(0, 4, [&](void* pData)
            {
                auto pVertex = static_cast<e10::vert_2d*>(pData);
                pVertex[0] = { -100.0f, -100.0f,  0.0f, 0.0f, 0xffffffff };
                pVertex[1] = { 100.0f, -100.0f,  1.0f, 0.0f, 0xffffffff };
                pVertex[2] = { 100.0f,  100.0f,  1.0f, 1.0f, 0xffffffff };
                pVertex[3] = { -100.0f,  100.0f,  0.0f, 1.0f, 0xffffffff };
            });
    }

    xgpu::buffer IndexBuffer;
    {
        if (auto Err = Device.Create(IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = 6 }); Err)
            return xgpu::getErrorInt(Err);

        (void)IndexBuffer.MemoryMap(0, 6, [&](void* pData)
            {
                auto            pIndex = static_cast<std::uint32_t*>(pData);
                constexpr auto  StaticIndex = std::array
                {
                    2u,  1u,  0u,      3u,  2u,  0u,    // front
                };
                static_assert(StaticIndex.size() == 6);
                for (auto i : StaticIndex)
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
    // Create the background material
    //
    xgpu::pipeline_instance BackgroundMaterialInstance;
    {
        xgpu::texture Texture;
        if (auto Err = xgpu::tools::bitmap::Create(Texture, Device, xcore::bitmap::getDefaultBitmap()); Err)
        {
            e10::DebugMessage(xgpu::getErrorMsg(Err));
            std::exit(xgpu::getErrorInt(Err));
        }

        auto  Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ Texture } };
        auto  Setup = xgpu::pipeline_instance::setup
        { .m_PipeLine = Pipeline
        , .m_SamplersBindings = Bindings
        };

        if (auto Err = Device.Create(BackgroundMaterialInstance, Setup); Err)
        {
            e10::DebugMessage(xgpu::getErrorMsg(Err));
            std::exit(xgpu::getErrorInt(Err));
        }
    }

    //
    // Setup the compiler
    //
    auto Compiler = std::make_unique<e10::compiler>();
    Compiler->SetupDescriptor("Texture", 0x34DBB69E8762EFA9);


    //
    // This will be the texture that we compiled
    //
    xgpu::pipeline_instance MaterialTexture;
    auto UpdateTextureMaterial = [&](bool isGamma)
    {
        //HACK: This is a huge hack to clear the MaterialTexture
        xgpu::pipeline_instance temp;
        std::memcpy(&MaterialTexture, &temp, sizeof(temp));

        xgpu::texture Texture;
        LoadTexture(Compiler->m_ResourcePath, Texture, Device, isGamma);

        auto  Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ Texture } };
        auto  Setup = xgpu::pipeline_instance::setup
        { .m_PipeLine = Pipeline
        , .m_SamplersBindings = Bindings
            
        };

        if (auto Err = Device.Create(MaterialTexture, Setup); Err)
        {
            e10::DebugMessage(xgpu::getErrorMsg(Err));
            std::exit(xgpu::getErrorInt(Err));
        }
    };

    //
    // Storate some input context
    //
    int             iActiveImage = 0;
    float           MouseScale = 1;
    xcore::vector2  MouseTranslate(0, 0);
    errors          Errors;

    //
    // Setup the inspector windows
    //
    xproperty::inspector InspectorViewer("Viewer");
    xproperty::inspector Inspector("Descriptor");
    Inspector.AppendEntity();
    Inspector.AppendEntityComponent(*xproperty::getObject(*Compiler->m_pInfo), Compiler->m_pInfo.get());
    Inspector.AppendEntityComponent(*Compiler->m_pDescriptor->getProperties(), Compiler->m_pDescriptor.get());


    InspectorViewer.AppendEntity();
    InspectorViewer.AppendEntityComponent(*xproperty::getObject(*Compiler.get()), Compiler.get());
    InspectorViewer.AppendEntityComponent(*xproperty::getObject(Errors), &Errors);

    //
    // Main loop
    //
    while (Instance.ProcessInputEvents())
    {
        if (xgpu::tools::imgui::BeginRendering(true))
            continue;

        //
        // Handle Input
        //
        if (auto ctx = ImGui::GetCurrentContext(); ctx->HoveredWindow == nullptr || ctx->HoveredWindow->ID == ImGui::GetID("MainDockSpace"))
        {
            const float  OldScale = MouseScale;
            auto& io = ImGui::GetIO();

            if (io.MouseDown[1] || io.MouseDown[2])
            {
                if (io.MouseDown[0])
                {
                    MouseScale -= 8000.0f * io.DeltaTime * (io.MouseDelta.y * (2.0f / MainWindow.getHeight()));
                }
                else
                {
                    MouseTranslate.m_X += io.MouseDelta.x * (2.0f / MainWindow.getWidth());
                    MouseTranslate.m_Y += io.MouseDelta.y * (2.0f / MainWindow.getHeight());
                }
            }

            // Wheel scale
            MouseScale += 0.9f * io.MouseWheel;
            if (MouseScale < 0.1f) MouseScale = 0.1f;

            // Always zoom from the perspective of the mouse
            const float mx = ((io.MousePos.x / (float)MainWindow.getWidth()) - 0.5f) * 2.0f;
            const float my = ((io.MousePos.y / (float)MainWindow.getHeight()) - 0.5f) * 2.0f;
            MouseTranslate.m_X += (MouseTranslate.m_X - mx) * (MouseScale - OldScale) / OldScale;
            MouseTranslate.m_Y += (MouseTranslate.m_Y - my) * (MouseScale - OldScale) / OldScale;

            // Make sure that the picture does not leave the view
            // should be 0.5 but I left a little bit of the picture inside the view with 0.4f
            const int W = static_cast<int>(MainWindow.getWidth()*0.49f), H = static_cast<int>(MainWindow.getHeight() * 0.65f);

            const float BorderX = ((W * 0.4f) / (float)MainWindow.getWidth())  * MouseScale;
            const float BorderY = ((H * 0.4f) / (float)MainWindow.getHeight()) * MouseScale;
            MouseTranslate.m_X = std::min(1.0f + BorderX, std::max(-1.0f - BorderX, MouseTranslate.m_X));
            MouseTranslate.m_Y = std::min(1.0f + BorderY, std::max(-1.0f - BorderY, MouseTranslate.m_Y));
        }

        //
        // Render the image
        //
        {
            auto CmdBuffer = MainWindow.getCmdBuffer();

            //
            // Render background
            //
            {
                CmdBuffer.setPipelineInstance(BackgroundMaterialInstance);
                CmdBuffer.setBuffer(VertexBuffer);
                CmdBuffer.setBuffer(IndexBuffer);

                e10::push_contants PushContants;

                PushContants.m_Scale =
                { (150 * 2.0f) / MainWindow.getWidth()
                , (150 * 2.0f) / MainWindow.getHeight()
                };
                PushContants.m_Translation.setZero();
                PushContants.m_UVScale = { 100.0f,100.0f };

                CmdBuffer.setPushConstants(PushContants);

                CmdBuffer.Draw(6);
            }

            //
            // Render Image
            //
            if( MaterialTexture.m_Private.get() )
            {
                CmdBuffer.setPipelineInstance(MaterialTexture);
                CmdBuffer.setBuffer(VertexBuffer);
                CmdBuffer.setBuffer(IndexBuffer);

                e10::push_contants PushContants;
                PushContants.m_Scale = { (MouseScale * 2.0f) / MainWindow.getWidth() * s_AspectRation
                                          , (MouseScale * 2.0f) / MainWindow.getHeight()
                };
                PushContants.m_UVScale.setup(1.0f, 1.0f);
                PushContants.m_Translation = MouseTranslate;

                CmdBuffer.setPushConstants(PushContants);

                CmdBuffer.Draw(6);
            }
        }


        //
        // Check if we should refresh the texture pipeline
        //
        if (Compiler->ReloadTexture())
        {
            UpdateTextureMaterial(static_cast<xtexture_rsc::descriptor*>(Compiler->m_pDescriptor.get())->m_bSRGB);
        }

        //
        // Render the Inspector
        //
        Errors.m_Errors.clear();
        Compiler->m_pDescriptor->Validate(Errors.m_Errors);

        Inspector.Show([]{});

        InspectorViewer.Show([]{});

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