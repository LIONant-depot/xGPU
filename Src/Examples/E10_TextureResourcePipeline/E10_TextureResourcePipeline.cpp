#include "../E05_Textures/E05_BitmapInspector.h"
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
    #include "x64\e10_vert.h"
};
constexpr auto g_FragShaderSPV = std::array
{
    #include "x64\e10_frag.h"
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
        float          m_MipLevel       {0};
        float          m_ToGamma        {1};
        xcore::vector2 m_Scale          {1};
        xcore::vector2 m_Translation    {0};
        xcore::vector2 m_UVScale        {1};
        xcore::vector4 m_TintColor      {1};
        xcore::vector4 m_ColorMask      {0};
        xcore::vector4 m_Mode           {0};
        xcore::vector4 m_NormalModes    {0};
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
    ( "Validation"
    , errors
    , obj_member_ro<"ValidationErrorList", &errors::m_Errors, member_ui_open<true> >\
    )
};
XPROPERTY_REG(errors)

//------------------------------------------------------------------------------------------------

#include "E10_Compiler.h"

//------------------------------------------------------------------------------------------------
// Give properties for xcore::vector2
//------------------------------------------------------------------------------------------------
struct vec2_friend : xcore::vector2
{
    XPROPERTY_DEF
    ("vector2", xcore::vector2, xproperty::settings::vector2_group
    , obj_member<"X", &xcore::vector2::m_X >
    , obj_member<"Y", &xcore::vector2::m_Y >
    )
};
XPROPERTY_REG(vec2_friend)

//------------------------------------------------------------------------------------------------

struct draw_controls
{
    float               m_MainWindowWidth;
    float               m_MainWindowHeight;
    float               m_MouseScale     = 1;
    xcore::vector2      m_MouseTranslate = { 0.0f, 0.0f };
    XPROPERTY_DEF
    ( "Controls", draw_controls
        , obj_member<"Image Location", &draw_controls::m_MouseTranslate, member_help<"Zooms in and out of the image"> >
        , obj_member
        < "Recenter"
        , +[](draw_controls& O, bool bRead, std::string& Value)
        {
            if (bRead) Value = "Recenter";
            else       O.m_MouseTranslate = { 0.0f, 0.0f };
        }
        , member_ui<std::string>::button<>
        , member_help<"Reset the image at the center of the screen"
        >>
        , obj_member < "Zoom", +[](draw_controls& O, bool bRead, float& Value)
        {
            if (bRead) Value = O.m_MouseScale;
            else
            {
                O.m_MouseTranslate.m_X += (O.m_MouseTranslate.m_X) * (Value - O.m_MouseScale) / O.m_MouseScale;
                O.m_MouseTranslate.m_Y += (O.m_MouseTranslate.m_Y) * (Value - O.m_MouseScale) / O.m_MouseScale;
                O.m_MouseScale = Value;
            }
        }
        , member_ui<float>::scroll_bar<1, 150>
        , member_help<"Zooms in and out of the image"
        >>
    )
};
XPROPERTY_REG(draw_controls)

//------------------------------------------------------------------------------------------------

struct draw_options
{
    enum class mode
    { COLOR_ALPHA
    , NO_ALPHA
    , A_ONLY
    , R_ONLY
    , G_ONLY
    , B_ONLY
    };

    static constexpr auto mode_v = std::array
    { xproperty::settings::enum_item("COLOR + ALPHA",   mode::COLOR_ALPHA,  "Render the image normally using all the channels available")
    , xproperty::settings::enum_item("NO_ALPHA",        mode::NO_ALPHA,     "Render the image normally using all available channels except Alpha. Alpha will be set to opaque.")
    , xproperty::settings::enum_item("A_ONLY",          mode::A_ONLY,       "Renders only the alpha channel as a single color without any alpha")
    , xproperty::settings::enum_item("R_ONLY",          mode::R_ONLY,       "Renders only the red channel as a single color without any alpha")
    , xproperty::settings::enum_item("G_ONLY",          mode::G_ONLY,       "Renders only the green channel as a single color without any alpha")
    , xproperty::settings::enum_item("B_ONLY",          mode::B_ONLY,       "Renders only the blue channel as a single color without any alpha")
    };

    enum class display_gamma_mode
    { GAMMA
    , LINEAR
    , RAW_DATA_INFILE
    };

    static constexpr auto display_gamma_mode_v = std::array
    { xproperty::settings::enum_item("GAMMA",           display_gamma_mode::GAMMA,              "Normal rendering. This is how games and other applications render their images.")
    , xproperty::settings::enum_item("LINEAR",          display_gamma_mode::LINEAR,             "This is how the shader is receiving the image so that it can operate...")
    , xproperty::settings::enum_item("RAW_DATA_INFILE", display_gamma_mode::RAW_DATA_INFILE,    "This is the raw data in the file. Useful to see the kind of precision of the data.")
    };

    float               m_UVScale               = 1;
    float               m_BackgroundIntensity   = 0.76f;
    mode                m_Mode                  = mode::COLOR_ALPHA;
    bool                m_bBilinearMode         = false;
    int                 m_ChooseMipLevel        = -1;
    int                 m_MaxMipLevels          = 0;
    display_gamma_mode  m_DisplayInGammaMode    = display_gamma_mode::GAMMA;
    float               m_DisplayGamma          = 2.2f;
    bool                m_bCurrentImageIsGamma  = false;

    XPROPERTY_DEF
    ("Render", draw_options
    , obj_member<"Bilinear", &draw_options::m_bBilinearMode, member_help<"Render the texture with bilinear filtering or nearest"> >
    , obj_member<"UVScale", &draw_options::m_UVScale, member_ui<float>::scroll_bar<1, 10>, member_help<"Scales the UV of the image"> >
    , obj_member<"Render Mode", &draw_options::m_Mode, member_enum_span<mode_v>, member_help<"Renders the image following one of the rules selected"> >
    , obj_member<"Background Intensity", &draw_options::m_BackgroundIntensity, member_ui<float>::scroll_bar<0, 3>, member_help<"Changes the intensity of the background"> >
    , obj_member<"ChooseMip", +[](draw_options& O, bool bRead, int& Value)
        {
            if (bRead) Value = O.m_ChooseMipLevel;
            else       O.m_ChooseMipLevel = std::min( O.m_MaxMipLevels, Value);
        }, member_ui<int>::scroll_bar<-1, 20>, member_help<"Selects a particular mip to render the texture. If you set the value to -1 "
                                                           "then it will go back to using the texture with all the mips (trilinear when bilinear is enable)"> >
    , obj_member<"Display Mode", &draw_options::m_DisplayInGammaMode, member_enum_span<display_gamma_mode_v>
        ,  member_help<"This mode shows the image in gamma (This is the normal mode, also how all the displays works), or it can show the image in LINEAR which is how the shader receives the image."
        >>
    , obj_member
        < "Display Gamma"
        , &draw_options::m_DisplayGamma
        , member_ui<float>::scroll_bar<1, 4>
        , member_dynamic_flags < +[](const draw_options& O)
        {
            xproperty::flags::type Flags{};
            Flags.m_bDontShow = O.m_DisplayInGammaMode != display_gamma_mode::GAMMA;
            return Flags;
        } >
        , member_help<"Changes the display gamma for the image"
        >>

    )
};
XPROPERTY_REG(draw_options)

//------------------------------------------------------------------------------------------------
float s_AspectRation = 1;
bool LoadTexture(const std::string& ResourcePath, xgpu::texture& Texture, xgpu::device& Device, e05::bitmap_inspector& BmpInspector )
{
    BmpInspector.Load(ResourcePath.c_str());

/*

    xcore::bitmap* pBitmap;
    xcore::bitmap  StorageBitmap;
    std::wcout << L"Loading Texture: " << ResourcePath.c_str() << L"\n";

    if ( ResourcePath.find(".dds" ) != std::string::npos )
    {
        pBitmap = &StorageBitmap;
        if (auto pError = xbmp::tools::loader::LoadDSS(*pBitmap, ResourcePath.c_str()); pError)
        {
            e10::DebugMessage("Failed to load the resource dds");
            std::exit(-1);
        }
    }
    else if (ResourcePath.find(".xbmp") != std::string::npos)
    {
        if (auto Err = xcore::bitmap::SerializeLoad(pBitmap, xcore::string::To<wchar_t>(ResourcePath)); Err)
        {
            e10::DebugMessage("Failed to load the xbmp resource");
            std::exit(-1);
        }
    }
    else
    {
        e10::DebugMessage("Unknown file format");
        std::exit(-1);
    }
*/

    if (auto Err = xgpu::tools::bitmap::Create(Texture, Device, *BmpInspector.m_pBitmap); Err)
    {
        e10::DebugMessage(xgpu::getErrorMsg(Err));
        std::exit(xgpu::getErrorInt(Err));
    }

    s_AspectRation = BmpInspector.m_pBitmap->getAspectRatio();

    bool isGamma = BmpInspector.m_pBitmap->getColorSpace() != xcore::bitmap::color_space::LINEAR;

    return isGamma;
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
    // Define materials
    //
    xgpu::pipeline BilinearPipeline;
    xgpu::pipeline NearestPipeline;
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

        auto Shaders  = std::array<const xgpu::shader*, 2>{ &MyFragmentShader, & MyVertexShader };
        auto Samplers = std::array{ xgpu::pipeline::sampler{} };
        auto Setup    = xgpu::pipeline::setup
        {
            .m_VertexDescriptor  = VertexDescriptor
        ,   .m_Shaders           = Shaders
        ,   .m_PushConstantsSize = sizeof(e10::push_contants)
        ,   .m_Samplers          = Samplers
        ,   .m_DepthStencil      = {.m_bDepthTestEnable = false }
        ,   .m_Blend             = xgpu::pipeline::blend::getAlphaOriginal()
        };

        if (auto Err = Device.Create(BilinearPipeline, Setup); Err)
            return xgpu::getErrorInt(Err);

        Samplers[0].m_MipmapMin = xgpu::pipeline::sampler::mipmap_sampler::NEAREST;
        Samplers[0].m_MipmapMag = xgpu::pipeline::sampler::mipmap_sampler::NEAREST;
        if (auto Err = Device.Create(NearestPipeline, Setup); Err)
            return xgpu::getErrorInt(Err);

    }

    //
    // Setup the meshes
    //
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
    // Create the background material instance
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
        { .m_PipeLine = BilinearPipeline
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
    // Define the draw options
    //
    draw_options            DrawOptions;
    e05::bitmap_inspector   BitmapInspector;
    draw_controls           DrawControls;
    errors                  Errors;

    //
    // This will be the texture that we compiled
    //
    xgpu::pipeline_instance BilinearMaterialTexture;
    xgpu::pipeline_instance NearestMaterialTexture;
    auto UpdateTextureMaterial = [&]()
    {
        xgpu::texture Texture;
        DrawOptions.m_bCurrentImageIsGamma = LoadTexture(Compiler->m_ResourcePath, Texture, Device, BitmapInspector);

        // Set the max mip levels
        // Make sure the current level is within the range
        DrawOptions.m_MaxMipLevels = Texture.getMipCount()-1;
        if (DrawOptions.m_ChooseMipLevel >= DrawOptions.m_MaxMipLevels )
        {
            DrawOptions.m_ChooseMipLevel = std::min(DrawOptions.m_MaxMipLevels, std::max( 0, DrawOptions.m_MaxMipLevels - 1) );
        }

        //HACK: This is a huge hack to clear the MaterialTexture
        xgpu::pipeline_instance temp;
        std::memcpy(&BilinearMaterialTexture, &temp, sizeof(temp));
        std::memcpy(&NearestMaterialTexture,  &temp, sizeof(temp));

        auto  Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ Texture } };

        // Bilinear mode
        {
            auto  Setup = xgpu::pipeline_instance::setup
            { .m_PipeLine         = BilinearPipeline
            , .m_SamplersBindings = Bindings
            };

            if (auto Err = Device.Create(BilinearMaterialTexture, Setup); Err)
            {
                e10::DebugMessage(xgpu::getErrorMsg(Err));
                std::exit(xgpu::getErrorInt(Err));
            }
        }

        // Nearest mode
        {
            auto Setup = xgpu::pipeline_instance::setup
            { .m_PipeLine         = NearestPipeline
            , .m_SamplersBindings = Bindings
            };

            if (auto Err = Device.Create(NearestMaterialTexture, Setup); Err)
            {
                e10::DebugMessage(xgpu::getErrorMsg(Err));
                std::exit(xgpu::getErrorInt(Err));
            }
        }

    };

    //
    // Storate some input context
    //
    int             iActiveImage  = 0;

    //
    // Setup the inspector windows
    //
    auto                  Inspectors = std::array
    { xproperty::inspector("Descriptor")
    , xproperty::inspector("Viewer")
    };

    Inspectors[0].AppendEntity();
    Inspectors[0].AppendEntityComponent(*xproperty::getObject(*Compiler->m_pInfo), Compiler->m_pInfo.get());
    Inspectors[0].AppendEntityComponent(*Compiler->m_pDescriptor->getProperties(), Compiler->m_pDescriptor.get());

    Inspectors[1].AppendEntity();
    Inspectors[1].AppendEntityComponent(*xproperty::getObject(*Compiler.get()), Compiler.get());
    Inspectors[1].AppendEntityComponent(*xproperty::getObject(Errors), &Errors);
    Inspectors[1].AppendEntityComponent(*xproperty::getObject(DrawControls), &DrawControls);
    Inspectors[1].AppendEntityComponent(*xproperty::getObject(DrawOptions), &DrawOptions);
    Inspectors[1].AppendEntityComponent(*xproperty::getObject(BitmapInspector), &BitmapInspector );
    
    //
    // Main loop
    //
    while (Instance.ProcessInputEvents())
    {
        if (xgpu::tools::imgui::BeginRendering(true))
            continue;

        const float MainWindowWidth  = static_cast<float>(MainWindow.getWidth());
        const float MainWindowHeight = static_cast<float>(MainWindow.getHeight());

        //
        // Handle Input
        //
        if (auto ctx = ImGui::GetCurrentContext(); ctx->HoveredWindow == nullptr || ctx->HoveredWindow->ID == ImGui::GetID("MainDockSpace"))
        {
            const float  OldScale = DrawControls.m_MouseScale;
            auto& io = ImGui::GetIO();

            if (io.MouseDown[1] || io.MouseDown[2])
            {
                if (io.MouseDown[0])
                {
                    DrawControls.m_MouseScale -= 8000.0f * io.DeltaTime * (io.MouseDelta.y * (2.0f / MainWindowHeight));
                }
                else
                {
                    DrawControls.m_MouseTranslate.m_X += io.MouseDelta.x * (2.0f / MainWindowWidth);
                    DrawControls.m_MouseTranslate.m_Y += io.MouseDelta.y * (2.0f / MainWindowHeight);
                }
            }

            // Wheel scale
            DrawControls.m_MouseScale += 1.9f * io.MouseWheel;
            if (DrawControls.m_MouseScale < 0.1f) DrawControls.m_MouseScale = 0.1f;

            // Always zoom from the perspective of the mouse
            const float mx = ((io.MousePos.x / (float)MainWindowWidth) - 0.5f) * 2.0f;
            const float my = ((io.MousePos.y / (float)MainWindowHeight) - 0.5f) * 2.0f;
            DrawControls.m_MouseTranslate.m_X += (DrawControls.m_MouseTranslate.m_X - mx) * (DrawControls.m_MouseScale - OldScale) / OldScale;
            DrawControls.m_MouseTranslate.m_Y += (DrawControls.m_MouseTranslate.m_Y - my) * (DrawControls.m_MouseScale - OldScale) / OldScale;
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
                { (150 * 2.0f) / MainWindowWidth
                , (150 * 2.0f) / MainWindowHeight
                };
                PushContants.m_Translation.setZero();
                PushContants.m_UVScale = { 120.0f,120.0f };

                PushContants.m_ToGamma   = 2.2f;

                PushContants.m_ColorMask = xcore::vector4(1);
                PushContants.m_Mode      = xcore::vector4(0,0,1,1);
                PushContants.m_TintColor = xcore::vector4(DrawOptions.m_BackgroundIntensity
                                                        , DrawOptions.m_BackgroundIntensity
                                                        , DrawOptions.m_BackgroundIntensity
                                                        , 1);
                PushContants.m_MipLevel = 0;
                PushContants.m_NormalModes.setZero();

                CmdBuffer.setPushConstants(PushContants);

                CmdBuffer.Draw(6);
            }

            //
            // Render Image
            //
            if( BilinearMaterialTexture.m_Private.get() )
            {
                if (DrawOptions.m_bBilinearMode ) CmdBuffer.setPipelineInstance(BilinearMaterialTexture);
                else                              CmdBuffer.setPipelineInstance(NearestMaterialTexture);

                CmdBuffer.setBuffer(VertexBuffer);
                CmdBuffer.setBuffer(IndexBuffer);

                e10::push_contants PushContants;
                PushContants.m_Scale = { (DrawControls.m_MouseScale * 2.0f) / MainWindowWidth * s_AspectRation
                                       , (DrawControls.m_MouseScale * 2.0f) / MainWindowHeight
                };
                PushContants.m_UVScale = DrawOptions.m_UVScale; //.setup(1.0f, 1.0f);
                PushContants.m_Translation = DrawControls.m_MouseTranslate;

                const float MipMode = DrawOptions.m_ChooseMipLevel == -1 ? 1.0f : 0.0f;

                switch (DrawOptions.m_Mode)
                {
                case draw_options::mode::COLOR_ALPHA: 
                    PushContants.m_ColorMask = xcore::vector4(1);
                    PushContants.m_Mode      = xcore::vector4(1, 0, 0, MipMode);
                    break;
                case draw_options::mode::NO_ALPHA:
                    PushContants.m_ColorMask = xcore::vector4(1);
                    PushContants.m_Mode      = xcore::vector4(0, 0, 1, MipMode);
                    break;
                case draw_options::mode::A_ONLY:
                    PushContants.m_ColorMask = xcore::vector4(0, 0, 0, 1);
                    PushContants.m_Mode      = xcore::vector4(0, 1, 0, MipMode);
                    break;
                case draw_options::mode::R_ONLY:
                    PushContants.m_ColorMask = xcore::vector4(1, 0, 0, 0);
                    PushContants.m_Mode      = xcore::vector4(0, 1, 0, MipMode);
                    break;
                case draw_options::mode::G_ONLY:
                    PushContants.m_ColorMask = xcore::vector4(0, 1, 0, 0);
                    PushContants.m_Mode      = xcore::vector4(0, 1, 0, MipMode);
                    break;
                case draw_options::mode::B_ONLY:
                    PushContants.m_ColorMask = xcore::vector4(0, 0, 1, 0);
                    PushContants.m_Mode      = xcore::vector4(0, 1, 0, MipMode);
                    break;
                }

                if ( BitmapInspector.m_pBitmap->getFormat() == xcore::bitmap::format::BC3_81Y0X_NORMAL 
                        && DrawOptions.m_DisplayInGammaMode != draw_options::display_gamma_mode::RAW_DATA_INFILE)
                {
                    PushContants.m_NormalModes = xcore::vector4(1, 0, 0, 0);
                }
                else if (BitmapInspector.m_pBitmap->getFormat() == xcore::bitmap::format::BC5_8YX_NORMAL
                        && DrawOptions.m_DisplayInGammaMode != draw_options::display_gamma_mode::RAW_DATA_INFILE)
                {
                    PushContants.m_NormalModes = xcore::vector4(0, 1, 0, 0);
                }
                else
                {
                    PushContants.m_NormalModes = xcore::vector4(0, 0, 0, 0);
                }

                PushContants.m_MipLevel = static_cast<float>(std::max(0,DrawOptions.m_ChooseMipLevel));

                PushContants.m_TintColor = xcore::vector4(1);
                switch (DrawOptions.m_DisplayInGammaMode)
                {
                case draw_options::display_gamma_mode::GAMMA: 
                    PushContants.m_ToGamma = DrawOptions.m_DisplayGamma;
                    break;
                case draw_options::display_gamma_mode::LINEAR:
                    PushContants.m_ToGamma = 1;
                    break;
                case draw_options::display_gamma_mode::RAW_DATA_INFILE:
                    if (DrawOptions.m_bCurrentImageIsGamma)
                    {
                        PushContants.m_ToGamma   = 2.2f;
                    }
                    else
                    {
                        PushContants.m_ToGamma   = 1.0f;
                    }
                    break;
                }

                CmdBuffer.setPushConstants(PushContants);

                CmdBuffer.Draw(6);
            }
        }


        //
        // Check if we should refresh the texture pipeline
        //
        if (Compiler->ReloadTexture())
        {
            UpdateTextureMaterial();
        }

        //
        // Render the Inspectors
        //
        Errors.m_Errors.clear();
        Compiler->m_pDescriptor->Validate(Errors.m_Errors);

        for (auto& E: Inspectors )
        {
            E.Show([] {});
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