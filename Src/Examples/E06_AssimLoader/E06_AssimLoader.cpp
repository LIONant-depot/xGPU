
#include "ModelLoader.h"
#include "../../tools/xgpu_view.h"
#include "../../tools/xgpu_view_inline.h"
#include "../../tools/xgpu_xcore_bitmap_helpers.h"
#include "../../dependencies/xbmp_tools/src/xbmp_tools.h"

using draw_vert = xgpu::assimp::vertex;

namespace e06
{
    //------------------------------------------------------------------------------------------------
    static
    void DebugMessage(std::string_view View)
    {
        printf("%s\n", View.data());
    }

    //------------------------------------------------------------------------------------------------

    struct push_contants
    {
        xcore::matrix4 m_L2C;
    };
}

//------------------------------------------------------------------------------------------------

int E06_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = e06::DebugMessage, .m_pLogWarning = e06::DebugMessage }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);

    xgpu::vertex_descriptor VertexDescriptor;
    {
        auto Attributes = std::array
        {
            xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(draw_vert, m_Position.m_X )
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(draw_vert, m_Texcoord.m_X)
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(draw_vert, m_Color)
            ,   .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED
            }
        };
        auto Setup = xgpu::vertex_descriptor::setup
        {
            .m_VertexSize = sizeof(draw_vert)
        ,   .m_Attributes = Attributes
        };

        if (auto Err = Device.Create(VertexDescriptor, Setup); Err)
            return xgpu::getErrorInt(Err);
    }

    //
    // Define a material
    //
    xgpu::pipeline PipeLine;
    {
        xgpu::shader MyFragmentShader;
        {
            auto RawData = xgpu::shader::setup::raw_data
            { std::array
                {
                    #include "x64\draw_frag.h"
                }
            };

            if (auto Err = Device.Create(MyFragmentShader, { .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = RawData }); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader MyVertexShader;
        {
            auto RawData = xgpu::shader::setup::raw_data
            { std::array
                {
                    #include "x64\draw_vert.h"
                }
            };
            xgpu::shader::setup Setup
            {
                .m_Type   = xgpu::shader::type::bit::VERTEX
            ,   .m_Sharer = RawData
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
        ,   .m_PushConstantsSize = sizeof(e06::push_contants)
        ,   .m_Samplers          = Samplers
        };

        if (auto Err = Device.Create(PipeLine, Setup ); Err)
            return xgpu::getErrorInt(Err);
    }

    //
    // Setup the material instance
    //
    std::array<xgpu::pipeline_instance,2> PipeLineInstance;
    for( int i=0; i< PipeLineInstance.size(); ++i )
    {
        xgpu::texture Texture;
        xcore::bitmap Bitmap;
        
        if( auto Err = xbmp::tools::loader::LoadDSS(Bitmap, "../../Assets/demon-skull-textured/textures/TD_Checker_Base_Color.dds" ); Err )
            return xgpu::getErrorInt(Err);

        if( auto Err = xgpu::tools::bitmap::Create(Texture, Device, Bitmap); Err )
            return xgpu::getErrorInt(Err);
        
        auto  Bindings = std::array                     { xgpu::pipeline_instance::sampler_binding{ Texture } };
        auto  Setup    = xgpu::pipeline_instance::setup
        {   .m_PipeLine         = PipeLine
        ,   .m_SamplersBindings = Bindings
        };

        if( auto Err = Device.Create( PipeLineInstance[i], Setup ); Err )
            return xgpu::getErrorInt(Err);
    }


    xgpu::assimp::model_loader ModelLoader;

    if( auto Err = ModelLoader.Load( Device, "../../Assets/demon-skull-textured/source/Skull_textured.fbx"); Err )
    {
        assert(false);
        return -1;
    }

    //
    // Main loop
    //
    xgpu::tools::view View;

    xgpu::mouse Mouse;
    {
        Instance.Create(Mouse, {});
    }

    xcore::radian3 Angles
    { xcore::radian{ -0.230000168f }
    , xcore::radian{ -1.40999949f  }
    , xcore::radian{ 0.0f }
    };
    float          Distance = 122;
    while (Instance.ProcessInputEvents())
    {
        //
        // Input
        //
        if (Mouse.isPressed(xgpu::mouse::digital::BTN_RIGHT))
        {
            auto MousePos = Mouse.getValue(xgpu::mouse::analog::POS_REL);
            Angles.m_Pitch.m_Value -= 0.01f * MousePos[1];
            Angles.m_Yaw.m_Value -= 0.01f * MousePos[0];
        }

        Distance += -1.0f * Mouse.getValue(xgpu::mouse::analog::WHEEL_REL)[0];
        if (Distance < 2) Distance = 2;

        // Update the camera
        View.LookAt(Distance, Angles, { 0,0,0 });

        //
        // Rendering
        //
        if (MainWindow.BeginRendering())
            continue;

        // Update the view with latest window size
        View.setViewport({ 0, 0, MainWindow.getWidth(), MainWindow.getHeight() });

        {
            auto CmdBuffer = MainWindow.getCmdBuffer();
            CmdBuffer.setPipelineInstance(PipeLineInstance[0]);

            e06::push_contants PushConstants;
            xcore::matrix4 L2W;
            L2W.setIdentity();
            L2W.setScale({0.4f});

            PushConstants.m_L2C = View.getW2C() * L2W;
            CmdBuffer.setPushConstants( PushConstants );

            ModelLoader.Draw(CmdBuffer);
        }

        //
        // Pageflip the windows
        //
        MainWindow.PageFlip();
    }

    return 0;
}