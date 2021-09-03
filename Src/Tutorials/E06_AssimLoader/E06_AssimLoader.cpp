
#include "ModelLoader.h"

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
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT3
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(draw_vert, m_Texcoord.m_X)
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT2
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(draw_vert, m_Color)
            ,   .m_Format = xgpu::vertex_descriptor::format::UINT8_RGBA_NORMALIZED
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
        {
            constexpr auto          size_v = 32;
            xgpu::texture::setup    Setup;
            auto                    Mips   = std::array{ xgpu::texture::setup::mip{ size_v * size_v * sizeof(std::uint32_t) }};

            Setup.m_Height   = size_v;
            Setup.m_Width    = size_v;
            Setup.m_MipChain = Mips;

            auto TextureData = std::make_unique< std::array<std::uint32_t, size_v* size_v> >();
            if( i == 0) std::fill(TextureData->begin(), TextureData->end(), 0xffffffffu);
            else std::fill(TextureData->begin(), TextureData->end(), 0xffffu);

            Setup.m_Data = { reinterpret_cast<const std::byte*>(TextureData->data()), Mips[0].m_Size };

            if (auto Err = Device.Create(Texture, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto  Bindings = std::array                     { xgpu::pipeline_instance::sampler_binding{ Texture } };
        auto  Setup    = xgpu::pipeline_instance::setup
        {   .m_PipeLine         = PipeLine
        ,   .m_SamplersBindings = Bindings
        };

        if( auto Err = Device.Create( PipeLineInstance[i], Setup ); Err )
            return xgpu::getErrorInt(Err);
    }


    xgpu::assimp::model_loader ModelLoader;

    if( auto Err = ModelLoader.Load( Device, "../../Dependencies/assimp/test/models/blend/box.blend"); Err ) //FBX/box.fbx //spider.fbx
    {
        assert(false);
        return -1;
    }

    //
    // Main loop
    //
    while (Instance.ProcessInputEvents())
    {
        if (MainWindow.BeginRendering())
            continue;

        auto FinalMatrix = std::array
        { -0.90439f, -1.15167f,  0.79665f,  0.79506f
        ,  0.00000f, -2.07017f, -0.51553f, -0.51450f
        ,  2.23845f, -0.46530f,  0.32187f,  0.32122f
        ,  0.00000f,  0.00000f,  5.64243f,  5.83095f
        };

        {
            auto CmdBuffer = MainWindow.getCmdBuffer();
            CmdBuffer.setPipelineInstance(PipeLineInstance[0]);
            CmdBuffer.setConstants(xgpu::shader::type::VERTEX, 0, FinalMatrix.data(), static_cast<std::uint32_t>(sizeof(FinalMatrix)));
            ModelLoader.Draw(CmdBuffer);
        }

        //
        // Pageflip the windows
        //
        MainWindow.PageFlip();
    }

    return 0;
}