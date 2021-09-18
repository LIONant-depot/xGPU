
#include "ModelLoader.h"
#include "../../tools/xgpu_view.h"
#include "../../tools/xgpu_view_inline.h"
#include "../../tools/xgpu_xcore_bitmap_helpers.h"

using draw_vert = xgpu::assimp::vertex;

//------------------------------------------------------------------------------------------------
static
void DebugMessage(std::string_view View)
{
    printf("%s\n", View.data());
}

//------------------------------------------------------------------------------------------------

int E06_Example()
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
                    #include "draw_frag.h"
                }
            };

            if (auto Err = Device.Create(MyFragmentShader, { .m_Type = xgpu::shader::type::FRAGMENT, .m_Sharer = RawData }); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader MyVertexShader;
        {
            auto UniformConstans = std::array
            { static_cast<int>(sizeof(float) * 4 * 4)   // LocalToClip
            };
            auto RawData = xgpu::shader::setup::raw_data
            { std::array
                {
                    #include "draw_vert.h"
                }
            };
            xgpu::shader::setup Setup
            {
                .m_Type   = xgpu::shader::type::VERTEX
            ,   .m_Sharer = RawData
            ,   .m_InOrderUniformSizes = UniformConstans
            };

            if (auto Err = Device.Create(MyVertexShader, Setup ); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Shaders    = std::array<const xgpu::shader*, 2>{ &MyFragmentShader, &MyVertexShader };
        auto Samplers   = std::array{ xgpu::pipeline::sampler{} };
        auto Setup      = xgpu::pipeline::setup
        {
            .m_VertexDescriptor = VertexDescriptor
        ,   .m_Shaders          = Shaders
        ,   .m_Samplers         = Samplers
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
        
        if( auto Err = xgpu::tools::bitmap::Create(Texture, Device, xcore::bitmap::getDefaultBitmap()); Err )
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

    if( auto Err = ModelLoader.Load( Device, "../../Dependencies/assimp/test/models/FBX/spider.fbx"); Err ) //FBX/box.fbx //
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
            CmdBuffer.setConstants(xgpu::shader::type::VERTEX, 0, &View.getW2C(), static_cast<std::uint32_t>(sizeof(decltype(View.getW2C()))));
            ModelLoader.Draw(CmdBuffer);
        }

        //
        // Pageflip the windows
        //
        MainWindow.PageFlip();
    }

    return 0;
}