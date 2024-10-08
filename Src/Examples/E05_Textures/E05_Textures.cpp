#include "xGPU.h"
#include "xcore.h"
#include "../../tools/xgpu_imgui_breach.h"
#include "../../tools/xgpu_xcore_bitmap_helpers.h"
#include "../E04_Properties/xPropertyImGuiInspector.h"
#include "../../dependencies/xbmp_tools/src/xbmp_tools.h"
#include <format>

#include "imgui_internal.h"

constexpr auto g_VertShaderSPV = std::array
{
    #include "x64/imgui_vert.h"
};
constexpr auto g_FragShaderSPV = std::array
{
    #include "x64/draw_frag.h"
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
}

//------------------------------------------------------------------------------------------------

struct e05::bitmap_inspector
{
    void Load( const char* pFileName, xgpu::device& Device, xgpu::pipeline& Pipeline ) noexcept
    {
        //
        // Get the file name
        //
        m_FileName = pFileName;
        m_FileName = m_FileName.substr(m_FileName.find_last_of("/\\") + 1);

        //
        // Load file
        //
        if( auto Err = xbmp::tools::loader::LoadDSS( m_Bitmap, pFileName ); Err )
        {
            e05::DebugMessage( xbmp::tools::getErrorMsg(Err) );
            std::exit(xbmp::tools::getErrorInt(Err) );
        }

        //
        // Create Texture
        //
        xgpu::texture Texture;
        if( auto Err = xgpu::tools::bitmap::Create(Texture, Device, m_Bitmap ); Err )
        {
            e05::DebugMessage(xgpu::getErrorMsg(Err));
            std::exit(xgpu::getErrorInt(Err));
        }

        //
        // Create Pipeline Instance
        //
        {
            auto  Bindings  = std::array{ xgpu::pipeline_instance::sampler_binding{ Texture } };
            auto  Setup     = xgpu::pipeline_instance::setup
            { .m_PipeLine           = Pipeline
            , .m_SamplersBindings   = Bindings
            };

            if (auto Err = Device.Create(m_Instance, Setup); Err)
            {
                e05::DebugMessage(xgpu::getErrorMsg(Err));
                std::exit(xgpu::getErrorInt(Err));
            }
        }

        //
        // Set the mipmap sizes
        //
        m_MipMapSizes.resize(m_Bitmap.getMipCount());
        for (int i = 0; i < m_Bitmap.getMipCount(); ++i)
        {
            m_MipMapSizes[i].x = static_cast<float>(std::max(1u, m_Bitmap.getWidth() >> i));
            m_MipMapSizes[i].y = static_cast<float>(std::max(1u, m_Bitmap.getHeight() >> i));
        }
    }

    xgpu::pipeline_instance m_Instance;
    std::string             m_FileName;
    xcore::bitmap           m_Bitmap;
    std::vector<ImVec2>     m_MipMapSizes;

    XPROPERTY_DEF
    ( "Bitmap Info", bitmap_inspector
    , obj_member_ro< "FileName", &bitmap_inspector::m_FileName, member_help<"The file name of the image"> >
    , obj_member_ro< "Size", +[](bitmap_inspector& I) ->ImVec2&
    {
        static ImVec2 Out;
        Out.x = static_cast<float>(I.m_Bitmap.getWidth());
        Out.y = static_cast<float>(I.m_Bitmap.getHeight());
        return Out;
    }, member_help<"Image Size in pixels" > >
    , obj_member_ro< "Format",  +[](bitmap_inspector& I, bool bRead, std::string& Out )
    {
        assert(bRead);
        switch (I.m_Bitmap.getFormat())
        {
            case xcore::bitmap::format::R4G4B4A4:   Out = "R4G4B4A4"; break;
            case xcore::bitmap::format::R5G6B5:     Out = "R5G6B5"; break;
            case xcore::bitmap::format::B5G5R5A1:   Out = "B5G5R5A1"; break;
            case xcore::bitmap::format::R8G8B8:     Out = "R8G8B8"; break;
            case xcore::bitmap::format::R8G8B8U8:   Out = "R8G8B8U8"; break;
            case xcore::bitmap::format::R8G8B8A8:   Out = "R8G8B8A8"; break;
            case xcore::bitmap::format::B8G8R8A8:   Out = "B8G8R8A8"; break;
            case xcore::bitmap::format::B8G8R8U8:   Out = "B8G8R8U8"; break;
            case xcore::bitmap::format::U8R8G8B8:   Out = "U8R8G8B8"; break;

            case xcore::bitmap::format::BC1_4RGB:   Out = "BC1_4RGB / DXT1"; break;
            case xcore::bitmap::format::BC1_4RGBA1: Out = "BC1_4RGBA1 / DXT1"; break;
            case xcore::bitmap::format::BC2_8RGBA:  Out = "BC2_8RGBA / DXT3"; break;
            case xcore::bitmap::format::BC3_8RGBA:  Out = "BC3_8RGBA / DXT5"; break;

            default: Out = "Unexpected format"; break;
        }
    }, member_help<"Format from xcore bitmap of the image" > >
    , obj_member_ro< "DataSize", +[](bitmap_inspector& I, bool, std::uint64_t& Out)
    {
        Out = I.m_Bitmap.getDataSize();
    }, member_help<"Size in bytes of the image/file" > >
    , obj_member_ro< "HasAlphaChannel", +[](bitmap_inspector& I, bool, bool& Out)
    {
        Out = I.m_Bitmap.hasAlphaChannel();
    }, member_help<"Checks if the data has an alpha channel" > >
    , obj_member_ro< "HasAlphaInfo", +[](bitmap_inspector& I, bool, bool& Out)
    {
        Out = I.m_Bitmap.hasAlphaInfo();
    }, member_help<"Checks if the alpha channel has any useful data" > >
    , obj_member_ro< "SRGB", +[](bitmap_inspector& I, bool, bool& Out)
    {
        Out = !I.m_Bitmap.isLinearSpace();
    }, member_help<"Check if the data is in Gamma space (if true) or Linear Space (if false)" > >
    , obj_member_ro< "FrameCount", +[](bitmap_inspector& I, bool, int& Out)
    {
        Out = static_cast<int>(I.m_Bitmap.getFrameCount());
    }, member_help<"Tells how many frames there is in this file" > >
    , obj_member_ro< "FrameSizeBytes", +[](bitmap_inspector& I, bool, int& Out)
    {
        Out = static_cast<int>(I.m_Bitmap.getFrameSize());
    }, member_help<"How big a frame in the file is" > >
    , obj_member_ro< "isCubeMap", +[](bitmap_inspector& I, bool, bool& Out)
    {
        Out = I.m_Bitmap.isCubemap();
    }, member_help<"Check if the image is a cube map" > >
    , obj_member_ro< "NumFacesCubeMap", +[](bitmap_inspector& I, bool, int& Out)
    {
        Out = static_cast<int>(I.m_Bitmap.getFaceCount());
    }, member_help<"If we have loaded a cubemap how many faces the file contains" > >
    , obj_member_ro< "NumFacesFaceSizeBytes", +[](bitmap_inspector& I, bool, int& Out)
    {
        Out = static_cast<int>(I.m_Bitmap.getFaceSize());
    }, member_help<"How many bytes does a face of the cubemap is in this file" > >
    , obj_member_ro< "Mips", &bitmap_inspector::m_MipMapSizes, member_help<"Tells how many mip map the image has" > >
    );
};
XPROPERTY_REG2(bitmap_inspector_props, e05::bitmap_inspector)

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
    std::array<e05::bitmap_inspector, 7> BitmapInspector;
    constexpr auto TextureList = std::array
    { "../../Src/Examples/E05_Textures/Alita-FullColor.dds"
    , "../../Src/Examples/E05_Textures/Alita-FullColor-Mipmaps.dds"
    , "../../Src/Examples/E05_Textures/Alita-FullColorNoAlpha-Mipmaps.dds"
    , "../../Src/Examples/E05_Textures/Alita-DXT1-Mipmaps.dds"
    , "../../Src/Examples/E05_Textures/Alita-DXT1-NoAlpha-Mipmaps.dds"
    , "../../Src/Examples/E05_Textures/Alita-DXT3.dds"
    , "../../Src/Examples/E05_Textures/Alita-DXT5.dds"
    };
    static_assert( BitmapInspector.size() == TextureList.size() );

    for( int i=0; i< TextureList.size(); ++i )
    {
        BitmapInspector[i].Load(TextureList[i], Device, Pipeline);
    }

    //
    // Create the background material
    //
    xgpu::pipeline_instance BackgroundMaterialInstance;
    {
        xgpu::texture Texture;
        if (auto Err = xgpu::tools::bitmap::Create(Texture, Device, xcore::bitmap::getDefaultBitmap() ); Err)
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
    xcore::vector2  MouseTranslate(0,0);

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
        // Handle Input
        //
        if( auto ctx = ImGui::GetCurrentContext(); ctx->HoveredWindow == nullptr || ctx->HoveredWindow->ID == ImGui::GetID("MainDockSpace") )
        {
            const float  OldScale = MouseScale;
            auto&        io       = ImGui::GetIO();

            if ( io.MouseDown[1] || io.MouseDown[2] )
            {
                if( io.MouseDown[0] )
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
            if(MouseScale < 0.1f ) MouseScale = 0.1f;

            // Always zoom from the perspective of the mouse
            const float mx = ((io.MousePos.x / (float)MainWindow.getWidth()) - 0.5f) * 2.0f;
            const float my = ((io.MousePos.y / (float)MainWindow.getHeight()) - 0.5f) * 2.0f;
            MouseTranslate.m_X += (MouseTranslate.m_X - mx) * (MouseScale - OldScale) / OldScale;
            MouseTranslate.m_Y += (MouseTranslate.m_Y - my) * (MouseScale - OldScale) / OldScale;

            // Make sure that the picture does not leave the view
            // should be 0.5 but I left a little bit of the picture inside the view with 0.4f
            const float BorderX = ((BitmapInspector[iActiveImage].m_Bitmap.getWidth() * 0.4f) / (float)MainWindow.getWidth()) * MouseScale;
            const float BorderY = ((BitmapInspector[iActiveImage].m_Bitmap.getHeight() * 0.4f) / (float)MainWindow.getHeight()) * MouseScale;
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

                e05::push_contants PushContants;

                PushContants.m_Scale = 
                { (150 * 2.0f) / MainWindow.getWidth()
                , (150 * 2.0f) / MainWindow.getHeight()
                };
                PushContants.m_Translation.setZero();
                PushContants.m_UVScale = { 100.0f,100.0f };

                CmdBuffer.setPushConstants( PushContants );

                CmdBuffer.Draw(6);
            }

            //
            // Render Image
            //
            {
                CmdBuffer.setPipelineInstance(BitmapInspector[iActiveImage].m_Instance);
                CmdBuffer.setBuffer(VertexBuffer);
                CmdBuffer.setBuffer(IndexBuffer);

                e05::push_contants PushContants;
                PushContants.m_Scale = { (MouseScale * 2.0f) / MainWindow.getWidth()    * BitmapInspector[iActiveImage].m_Bitmap.getAspectRatio()
                                       , (MouseScale * 2.0f) / MainWindow.getHeight()
                                       };
                PushContants.m_UVScale.setup(1.0f, 1.0f);
                PushContants.m_Translation = MouseTranslate;

                CmdBuffer.setPushConstants( PushContants );

                CmdBuffer.Draw( 6 );
            }
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
        Inspector.Show([&]
        {
            if (ImGui::Combo("Select File", &iActiveImage, TextureList.data(), static_cast<int>(TextureList.size())))
            {
                Inspector.clear();
                Inspector.AppendEntity();
                Inspector.AppendEntityComponent(*xproperty::getObject(BitmapInspector[iActiveImage]), &BitmapInspector[iActiveImage]);
            }
        });


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

